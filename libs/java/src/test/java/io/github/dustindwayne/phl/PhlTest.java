package io.github.dustindwayne.phl;

import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertFalse;
import static org.junit.jupiter.api.Assertions.assertNotNull;
import static org.junit.jupiter.api.Assertions.assertNull;
import static org.junit.jupiter.api.Assertions.assertThrows;
import static org.junit.jupiter.api.Assertions.assertTrue;

import java.io.IOException;
import java.io.StringReader;
import java.math.BigDecimal;
import java.math.BigInteger;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.nio.file.Path;
import java.nio.file.Paths;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.Collections;
import java.util.LinkedHashMap;
import java.util.List;
import java.util.Map;
import java.util.stream.Collectors;
import java.util.stream.Stream;
import org.junit.jupiter.api.DynamicTest;
import org.junit.jupiter.api.Test;
import org.junit.jupiter.api.TestFactory;

class PhlTest {
    private static final Path CONFORMANCE = Paths.get("..", "..", "conformance");

    private static String read(Path path) throws IOException {
        return new String(Files.readAllBytes(path), StandardCharsets.UTF_8);
    }

    private static List<Path> cases(String folder, String extension) throws IOException {
        try (Stream<Path> files = Files.list(CONFORMANCE.resolve(folder))) {
            return files.filter(p -> p.getFileName().toString().endsWith(extension)).sorted().collect(Collectors.toList());
        }
    }

    private static Path sibling(Path path, String extension) {
        String name = path.getFileName().toString();
        return path.resolveSibling(name.substring(0, name.lastIndexOf('.')) + extension);
    }

    /** A small JSON reader for the expected results of the conformance cases. */
    private static final class Json {
        private final String text;
        private int pos = 0;

        Json(String text) {
            this.text = text;
        }

        private void space() {
            while (pos < text.length() && " \n\r\t".indexOf(text.charAt(pos)) >= 0) {
                pos++;
            }
        }

        Object read() {
            space();
            char c = text.charAt(pos);
            if (c == '{') {
                pos++;
                Map<String, Object> result = new LinkedHashMap<>();
                while (true) {
                    space();
                    if (text.charAt(pos) == '}') {
                        pos++;
                        return result;
                    }
                    if (text.charAt(pos) == ',') {
                        pos++;
                        continue;
                    }
                    String key = string();
                    space();
                    pos++;
                    result.put(key, read());
                }
            }
            if (c == '[') {
                pos++;
                List<Object> result = new ArrayList<>();
                while (true) {
                    space();
                    if (text.charAt(pos) == ']') {
                        pos++;
                        return result;
                    }
                    if (text.charAt(pos) == ',') {
                        pos++;
                        continue;
                    }
                    result.add(read());
                }
            }
            if (c == '"') {
                return string();
            }
            if (text.startsWith("true", pos)) {
                pos += 4;
                return Boolean.TRUE;
            }
            if (text.startsWith("false", pos)) {
                pos += 5;
                return Boolean.FALSE;
            }
            if (text.startsWith("null", pos)) {
                pos += 4;
                return null;
            }
            int start = pos;
            while (pos < text.length() && "-.0123456789".indexOf(text.charAt(pos)) >= 0) {
                pos++;
            }
            String number = text.substring(start, pos);
            if (number.indexOf('.') >= 0) {
                return Double.valueOf(number);
            }
            return Long.valueOf(number);
        }

        private String string() {
            StringBuilder out = new StringBuilder();
            pos++;
            while (text.charAt(pos) != '"') {
                char c = text.charAt(pos++);
                if (c == '\\') {
                    c = text.charAt(pos++);
                    if (c == 'n') {
                        c = '\n';
                    } else if (c == 't') {
                        c = '\t';
                    }
                }
                out.append(c);
            }
            pos++;
            return out.toString();
        }
    }

    private static Object json(Path path) throws IOException {
        return new Json(read(path)).read();
    }

    /** Equal, and with the same key order all the way down. */
    private static void assertSame(Object expected, Object actual) {
        assertEquals(expected, actual);
        assertEquals(flatten(expected), flatten(actual));
    }

    private static String flatten(Object value) {
        StringBuilder out = new StringBuilder();
        List<Object> pending = new ArrayList<>();
        pending.add(value);
        while (!pending.isEmpty()) {
            Object item = pending.remove(pending.size() - 1);
            if (item instanceof Map) {
                out.append(((Map<?, ?>) item).keySet());
                pending.addAll(((Map<?, ?>) item).values());
            } else if (item instanceof List) {
                out.append('[').append(((List<?>) item).size());
                pending.addAll((List<?>) item);
            } else {
                out.append(item == null ? "null" : item.getClass().getSimpleName() + ":" + item).append(';');
            }
        }
        return out.toString();
    }

    @TestFactory
    List<DynamicTest> conformance() throws IOException {
        List<DynamicTest> tests = new ArrayList<>();
        for (Path source : cases("phl/valid", ".phl")) {
            tests.add(DynamicTest.dynamicTest(source.getFileName().toString(), () -> {
                String text = read(source);
                Map<String, Object> data = Phl.parse(text);
                assertSame(json(sibling(source, ".json")), data);
                Path raw = sibling(source, ".nocoerce.json");
                if (Files.exists(raw)) {
                    assertSame(json(raw), Phl.parse(text, false));
                }
                for (int indent : new int[] {4, 0}) {
                    assertSame(data, Phl.parse(Phl.stringify(data, indent)));
                }
            }));
        }
        for (Path source : cases("phl/invalid", ".phl")) {
            tests.add(DynamicTest.dynamicTest(source.getFileName().toString(), () -> {
                String text = read(source);
                assertThrows(PhlException.class, () -> Phl.parse(text));
            }));
        }
        for (Path source : cases("hpl/valid", ".hpl")) {
            tests.add(DynamicTest.dynamicTest(source.getFileName().toString(), () -> {
                List<Map<String, Object>> rows = Phl.parseTable(read(source));
                assertSame(json(sibling(source, ".json")), rows);
                assertSame(rows, Phl.parseTable(Phl.stringifyTable(rows)));
            }));
        }
        for (Path source : cases("hpl/invalid", ".hpl")) {
            tests.add(DynamicTest.dynamicTest(source.getFileName().toString(), () -> {
                String text = read(source);
                assertThrows(PhlException.class, () -> Phl.parseTable(text));
            }));
        }
        return tests;
    }

    private static PhlException failure(String text) {
        return assertThrows(PhlException.class, () -> Phl.parse(text));
    }

    private static Map<String, Object> map(Object... pairs) {
        Map<String, Object> result = new LinkedHashMap<>();
        for (int i = 0; i < pairs.length; i += 2) {
            result.put((String) pairs[i], pairs[i + 1]);
        }
        return result;
    }

    @Test
    void types() {
        Map<String, Object> doc = Phl.parse("whole: 42\ndecimal: 34.00\nnegative: -0.5\na: \"True\"\nb: \"NULL\"\n\"true\": 1");
        assertEquals(42L, doc.get("whole"));
        assertEquals(34.0, doc.get("decimal"));
        assertEquals(-0.5, doc.get("negative"));
        assertEquals(Boolean.TRUE, doc.get("a"));
        assertTrue(doc.containsKey("b"));
        assertNull(doc.get("b"));
        assertEquals(1L, doc.get("true"));
    }

    @Test
    void coercionOnlyMatchesAscii() {
        // String.equalsIgnoreCase would treat these as "nil" and "false".
        assertEquals("n\u0131l", Phl.parse("a: \"n\u0131l\"").get("a"));
        assertEquals("fal\u017Fe", Phl.parse("a: \"fal\u017Fe\"").get("a"));
    }

    @Test
    void bareWordsAreErrors() {
        for (String word : new String[] {"true", "false", "nil"}) {
            failure("a: " + word);
        }
    }

    @Test
    @SuppressWarnings("unchecked")
    void tableIndex() {
        Map<String, Object> doc = Phl.parse("product: (ID, Name) { 101, \"a\"; 102, \"b\"; 103, \"c\"; 104, \"d\"; }");
        List<Map<String, Object>> rows = (List<Map<String, Object>>) doc.get("product");
        assertEquals(104L, rows.get(3).get("ID"));
    }

    @Test
    void errorPositions() {
        PhlException error = failure("a: 1\nb: oops");
        assertEquals(2, error.getLine());
        assertEquals(4, error.getColumn());
        assertEquals("unexpected word 'oops'; strings need double quotes", error.getReason());
        assertEquals("unexpected word 'oops'; strings need double quotes (line 2, column 4)", error.getMessage());

        assertEquals(10, failure("a: \"\uD83D\uDE00\uD83D\uDE00\u00E9\" oops").getColumn());
        assertEquals(4, failure("\uFEFFa: oops").getColumn());
        assertEquals(8, failure("a: \"x\" \"y\"").getColumn());
        assertEquals(0, assertThrows(PhlException.class, () -> Phl.stringify(map("x", new Object()))).getLine());
    }

    @Test
    void numbers() {
        Map<String, Object> doc = Phl.parse("max: 9223372036854775807\nmin: -9223372036854775808\nzeros: 007\nnegzero: -0");
        assertEquals(Long.MAX_VALUE, doc.get("max"));
        assertEquals(Long.MIN_VALUE, doc.get("min"));
        assertEquals(7L, doc.get("zeros"));
        assertEquals(0L, doc.get("negzero"));

        assertEquals(4, failure("a: 9223372036854775808").getColumn());
        assertEquals(4, failure("a: -9223372036854775809").getColumn());
        assertTrue(Phl.parse("123456789012345678901234567890: 1").containsKey("123456789012345678901234567890"));

        String zeros = String.join("", Collections.nCopies(400, "0"));
        String nines = String.join("", Collections.nCopies(400, "9"));
        assertEquals(4, failure("a: " + nines + ".0").getColumn());
        assertEquals(0.0, Phl.parse("a: 0." + zeros + "1").get("a"));

        for (double number : new double[] {5e-324, 1e-310, 2.2250738585072014e-308, 1.7976931348623157e308, 0.1 + 0.2, 1.0 / 3.0}) {
            assertEquals(number, Phl.parse(Phl.stringify(map("a", number))).get("a"));
        }
    }

    @Test
    void layout() {
        Map<String, Object> doc = map(
                "name", "Ann",
                "tags", Arrays.asList("a", "b"),
                "server", map("Host", "db", "Port", 1341),
                "rows", Arrays.asList(map("ID", 1, "Name", "x"), map("ID", 2, "Name", "y")));
        assertEquals(
                "name: \"Ann\"\n"
                        + "tags: [\"a\", \"b\"]\n"
                        + "server: {\n"
                        + "    Host: \"db\",\n"
                        + "    Port: 1341\n"
                        + "}\n"
                        + "rows: (ID, Name) {\n"
                        + "    1, \"x\";\n"
                        + "    2, \"y\";\n"
                        + "}\n",
                Phl.stringify(doc));
        assertEquals(
                "name: \"Ann\"\n"
                        + "tags: [\"a\", \"b\"]\n"
                        + "server: {Host: \"db\", Port: 1341}\n"
                        + "rows: (ID, Name) { 1, \"x\"; 2, \"y\"; }\n",
                Phl.stringify(doc, 0));
    }

    @Test
    void writing() {
        assertEquals("\"a b\": \"say \\\"hi\\\"\\n\"\n", Phl.stringify(map("a b", "say \"hi\"\n")));
        assertEquals("a: \"true\"\nb: \"false\"\nc: \"nil\"\n", Phl.stringify(map("a", true, "b", false, "c", null)));
        assertEquals(
                "small: 0.0000001\nlarge: 10000000000000000000000.0\nwhole: 34.0\ntenth: 0.1\nneg: -25.99\nzero: -0.0\n",
                Phl.stringify(map("small", 1e-7, "large", 1e22, "whole", 34.0, "tenth", 0.1, "neg", -25.99, "zero", -0.0)));
        assertEquals("o: {}\nl: []\n", Phl.stringify(Phl.parse("o: {}\nl: []")));
        assertEquals("", Phl.stringify(map()));
        assertEquals("(ID, \"full name\") {\n    1, \"Ann\";\n}\n", Phl.stringifyTable(Phl.parseTable("(ID, \"full name\") { 1, \"Ann\"; }")));
        assertEquals("(ID) { 1; 2; }\n", Phl.stringifyTable(Arrays.asList(map("ID", 1), map("ID", 2)), 0));
    }

    @Test
    void writingOtherJavaTypes() {
        assertEquals("a: 1.5\n", Phl.stringify(map("a", 1.5f)));
        assertEquals("a: 0.1\n", Phl.stringify(map("a", 0.1f)));
        assertEquals("a: 1.25\n", Phl.stringify(map("a", new BigDecimal("1.25"))));
        assertEquals("a: 5.0\n", Phl.stringify(map("a", new BigDecimal("5"))));
        assertEquals("a: 1000.0\n", Phl.stringify(map("a", new BigDecimal("1E+3"))));
        assertEquals("a: 7\n", Phl.stringify(map("a", BigInteger.valueOf(7))));
        assertEquals("a: [1, 2]\n", Phl.stringify(map("a", new int[] {1, 2})));
        assertEquals("a: [\"x\", \"y\"]\n", Phl.stringify(map("a", new String[] {"x", "y"})));
        assertEquals("a: \"x\"\n", Phl.stringify(map("a", new StringBuilder("x"))));
        assertEquals("a: (b) {\n    1;\n}\n", Phl.stringify(map("a", Collections.singleton(map("b", 1)))));
    }

    @Test
    void refusesWhatCannotBeWritten() {
        Map<Object, Object> numericKey = new LinkedHashMap<>();
        numericKey.put(1, "a");
        for (Object bad : new Object[] {
            new Object(),
            Double.NaN,
            Double.POSITIVE_INFINITY,
            Float.NaN,
            "\u0001",
            BigInteger.ONE.shiftLeft(64),
            numericKey,
        }) {
            assertThrows(PhlException.class, () -> Phl.stringify(map("x", bad)), String.valueOf(bad));
        }
        assertThrows(PhlException.class, () -> Phl.stringify(null));
        assertThrows(PhlException.class, () -> Phl.stringifyTable(new ArrayList<Map<String, Object>>()));
        assertThrows(PhlException.class, () -> Phl.stringifyTable(Arrays.asList(map("a", 1), map("b", 2))));
    }

    @Test
    void nestingLimit() {
        String open = String.join("", Collections.nCopies(Phl.MAX_DEPTH, "["));
        String close = String.join("", Collections.nCopies(Phl.MAX_DEPTH, "]"));
        Map<String, Object> deep = Phl.parse("a: " + open + close);
        assertNotNull(Phl.stringify(deep));
        failure("a: [" + open + close + "]");
        failure("a: " + String.join("", Collections.nCopies(200000, "[")));

        // A value that contains itself fails with an error, not a stack overflow.
        List<Object> cycle = new ArrayList<>();
        cycle.add(cycle);
        assertThrows(PhlException.class, () -> Phl.stringify(map("a", cycle)));
    }

    @Test
    void readers() throws IOException {
        assertEquals(1L, Phl.parse(new StringReader("a: 1")).get("a"));
        assertEquals(1L, Phl.parseTable(new StringReader("(a) { 1; }")).get(0).get("a"));
    }

    @Test
    void valuesThatLookLikeOtherTypesStayStrings() {
        assertEquals("007", Phl.parse("a: \"007\"").get("a"));
        assertFalse(Phl.parse("a: \"yes\"").get("a") instanceof Boolean);
    }
}
