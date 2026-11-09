# phl for Java, Kotlin and Scala

Reads and writes PHL documents (`.phl`, `.pll`) and HPL tables (`.hpl`,
`.hpll`). Java 8 or newer, no dependencies. The same library works from Kotlin,
Scala and Android.

```xml
<dependency>
  <groupId>io.github.d-ustindwayne</groupId>
  <artifactId>phl-file</artifactId>
  <version>0.1.0</version>
</dependency>
```

```java
import io.github.dustindwayne.phl.Phl;

Map<String, Object> doc = Phl.parse("name: \"Ann\"\nproduct: (ID, Name) { 101, \"Mouse\"; 102, \"Chair\"; }");
List<Map<String, Object>> rows = (List<Map<String, Object>>) doc.get("product");
rows.get(1).get("Name");   // "Chair"

String text = Phl.stringify(doc);

List<Map<String, Object>> table = Phl.parseTable(hplText);
Phl.stringifyTable(table);
```

Kotlin:

```kotlin
val doc = Phl.parse(text)
val rows = doc["product"] as List<Map<String, Any?>>
println(rows[1]["Name"])

println(Phl.stringify(mapOf("name" to "Ann", "tags" to listOf("a", "b"))))
```

Scala works with Java's collections, so convert with
`scala.jdk.CollectionConverters`:

```scala
import scala.jdk.CollectionConverters.*

val doc = Phl.parse(text).asScala
println(doc("name"))

println(Phl.stringify(Map[String, Object]("name" -> "Ann", "tags" -> Seq("a", "b").asJava).asJava))
```

| Method | Does |
|---|---|
| `Phl.parse(text)`, `Phl.parse(text, coerce)`, `Phl.parse(reader)` | PHL text to a `Map` |
| `Phl.parseTable(text)`, `Phl.parseTable(text, coerce)`, `Phl.parseTable(reader)` | HPL text to a `List` of `Map`s |
| `Phl.stringify(map)`, `Phl.stringify(map, indent)` | a `Map` to PHL text |
| `Phl.stringifyTable(rows)`, `Phl.stringifyTable(rows, indent)` | maps with the same keys to HPL text |

With `coerce` on, which is the default, the strings `"true"` and `"false"` become
booleans, and `"nil"`, `"null"` and `"none"` become `null`, ignoring case.

Values are read as `LinkedHashMap` (keys keep their order), `ArrayList`, `String`,
`Long`, `Double`, `Boolean` and `null`. Whole numbers beyond 64 bits and decimals
too large for a `double` are errors.

When writing, values can also be any `Map` with string keys, `Iterable`, array or
`CharSequence`, and `byte`, `short`, `int`, `float`, `BigDecimal`, or a `BigInteger`
that fits in 64 bits. `indent` is the spaces per nesting level, and 0 writes nested
values on one line. Maps with the same keys in the same order are written as a
table, so use a map that keeps its order, such as `LinkedHashMap`.

Errors throw `PhlException`, a `RuntimeException`, with `getLine()`, `getColumn()`
and `getReason()` (the message without the position). The line and column are 0
when there is no position, and columns count Unicode code points. Nesting deeper
than `Phl.MAX_DEPTH` (256) is an error when reading and writing.

## Tests

```
mvn test
mvn -Pinterop test
```

The tests run the shared cases in `../../conformance` as well. `-Pinterop` also
compiles and runs the tests in `src/interop`, written in Kotlin and Scala.
