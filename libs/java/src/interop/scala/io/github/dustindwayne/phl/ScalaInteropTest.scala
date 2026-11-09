package io.github.dustindwayne.phl

import org.junit.jupiter.api.Assertions.{assertEquals, assertNull, assertThrows}
import org.junit.jupiter.api.Test
import scala.jdk.CollectionConverters.*

// Uses the library the way Scala code would: Scala collections are converted
// with scala.jdk.CollectionConverters, as for any Java library.
class ScalaInteropTest:
  @Test
  def readsIntoScalaTypes(): Unit =
    val doc = Phl.parse("name: \"Ann\"\nage: 30\nnote: \"nil\"\nproduct: (ID, Name) { 101, \"a\"; 102, \"b\"; }").asScala
    assertEquals("Ann", doc("name"))
    assertEquals(30L, doc("age"))
    assertNull(doc("note"))

    val rows = doc("product").asInstanceOf[java.util.List[java.util.Map[String, Object]]].asScala
    assertEquals("b", rows(1).get("Name"))
    assertEquals(Seq(101L, 102L), rows.map(_.get("ID")).toSeq)

  @Test
  def writesScalaCollections(): Unit =
    val doc: Map[String, Object] = Map(
      "name" -> "Ann",
      "tags" -> Seq("a", "b").asJava,
      "array" -> Array("x"),
      "rows" -> Seq(Map[String, Object]("ID" -> Integer.valueOf(1)).asJava, Map[String, Object]("ID" -> Integer.valueOf(2)).asJava).asJava
    )
    val text = Phl.stringify(doc.asJava)
    assertEquals("Ann", Phl.parse(text).get("name"))
    assertEquals(
      "(a) {\n    1;\n}\n",
      Phl.stringifyTable(Seq(Map[String, Object]("a" -> Integer.valueOf(1)).asJava).asJava)
    )

  @Test
  def errorsAreCatchable(): Unit =
    val error = assertThrows(classOf[PhlException], () => Phl.parse("a: oops"))
    assertEquals(1, error.getLine)
    assertEquals(4, error.getColumn)
