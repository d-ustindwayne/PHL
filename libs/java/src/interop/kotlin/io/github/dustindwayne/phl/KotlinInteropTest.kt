package io.github.dustindwayne.phl

import org.junit.jupiter.api.Assertions.assertEquals
import org.junit.jupiter.api.Assertions.assertNull
import org.junit.jupiter.api.Assertions.assertThrows
import org.junit.jupiter.api.Test

// Uses the library the way Kotlin code would: with Kotlin's own maps and lists.
class KotlinInteropTest {
    @Test
    fun readsIntoKotlinTypes() {
        val doc: Map<String, Any?> = Phl.parse("name: \"Ann\"\nage: 30\nnote: \"nil\"\nproduct: (ID, Name) { 101, \"a\"; 102, \"b\"; }")
        assertEquals("Ann", doc["name"])
        assertEquals(30L, doc["age"])
        assertNull(doc["note"])

        @Suppress("UNCHECKED_CAST")
        val rows = doc["product"] as List<Map<String, Any?>>
        assertEquals("b", rows[1]["Name"])
        assertEquals(listOf(101L, 102L), rows.map { it["ID"] })
    }

    @Test
    fun writesKotlinCollections() {
        val doc = mapOf(
            "name" to "Ann",
            "tags" to listOf("a", "b"),
            "set" to setOf(1, 2),
            "array" to arrayOf("x"),
            "ints" to intArrayOf(1, 2),
            "nothing" to null,
            "yes" to true,
            "rows" to listOf(mapOf("ID" to 1), mapOf("ID" to 2)),
        )
        assertEquals(
            """
            name: "Ann"
            tags: ["a", "b"]
            set: [1, 2]
            array: ["x"]
            ints: [1, 2]
            nothing: "nil"
            yes: "true"
            rows: (ID) {
                1;
                2;
            }

            """.trimIndent(),
            Phl.stringify(doc),
        )
        assertEquals("(a) {\n    1;\n}\n", Phl.stringifyTable(listOf(mapOf("a" to 1))))
        assertEquals(1L, Phl.parseTable("(a) { 1; }")[0]["a"])
    }

    @Test
    fun errorsAreCatchable() {
        val error = assertThrows(PhlException::class.java) { Phl.parse("a: oops") }
        assertEquals(1, error.line)
        assertEquals(4, error.column)
        assertEquals("unexpected word 'oops'; strings need double quotes", error.reason)
    }
}
