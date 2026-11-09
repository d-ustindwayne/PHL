package io.github.dustindwayne.phl;

import java.io.IOException;
import java.io.Reader;
import java.util.List;
import java.util.Map;

/**
 * Reads and writes PHL documents (.phl, .pll) and HPL tables (.hpl, .hpll).
 *
 * <p>Values are read as {@link Map} (keys keep their order), {@link List},
 * {@link String}, {@link Long}, {@link Double}, {@link Boolean} and
 * {@code null}. Whole numbers beyond 64 bits are an error.
 *
 * <pre>{@code
 * Map<String, Object> doc = Phl.parse(text);
 * String text = Phl.stringify(doc);
 * }</pre>
 */
public final class Phl {
    /** Lists, objects and tables nested deeper than this are an error. */
    public static final int MAX_DEPTH = 256;

    private static final int DEFAULT_INDENT = 4;

    private Phl() {}

    /**
     * Reads a PHL document. The quoted strings "true" and "false" become booleans,
     * and "nil", "null" and "none" become null, ignoring case.
     *
     * @param text the document
     * @return the top-level entries
     * @throws PhlException if the text is not valid PHL
     */
    public static Map<String, Object> parse(String text) {
        return parse(text, true);
    }

    /**
     * Reads a PHL document.
     *
     * @param text the document
     * @param coerce whether to convert "true", "false", "nil", "null" and "none"
     * @return the top-level entries
     * @throws PhlException if the text is not valid PHL
     */
    public static Map<String, Object> parse(String text, boolean coerce) {
        return new Parser(text, coerce).document();
    }

    /**
     * Reads a PHL document until the end of the reader.
     *
     * @param reader where to read from; it is not closed
     * @return the top-level entries
     * @throws IOException if reading fails
     * @throws PhlException if the text is not valid PHL
     */
    public static Map<String, Object> parse(Reader reader) throws IOException {
        return parse(readAll(reader), true);
    }

    /**
     * Reads an HPL table, with the same conversions as {@link #parse(String)}.
     *
     * @param text the table
     * @return one map for each row
     * @throws PhlException if the text is not a valid table
     */
    public static List<Map<String, Object>> parseTable(String text) {
        return parseTable(text, true);
    }

    /**
     * Reads an HPL table.
     *
     * @param text the table
     * @param coerce whether to convert "true", "false", "nil", "null" and "none"
     * @return one map for each row
     * @throws PhlException if the text is not a valid table
     */
    public static List<Map<String, Object>> parseTable(String text, boolean coerce) {
        return new Parser(text, coerce).tableDocument();
    }

    /**
     * Reads an HPL table until the end of the reader.
     *
     * @param reader where to read from; it is not closed
     * @return one map for each row
     * @throws IOException if reading fails
     * @throws PhlException if the text is not a valid table
     */
    public static List<Map<String, Object>> parseTable(Reader reader) throws IOException {
        return parseTable(readAll(reader), true);
    }

    /**
     * Writes a PHL document with 4 spaces per nesting level.
     *
     * @param document the top-level entries
     * @return the text
     * @throws PhlException if a value cannot be written
     * @see #stringify(Map, int)
     */
    public static String stringify(Map<String, ?> document) {
        return stringify(document, DEFAULT_INDENT);
    }

    /**
     * Writes a PHL document. A list of maps that all have the same keys in the
     * same order is written as a table, and booleans and null as "true", "false"
     * and "nil".
     *
     * <p>Values can be a {@link Map} with string keys, an {@link Iterable}, an
     * array, a {@link CharSequence}, a {@link Boolean}, null, or a number: byte,
     * short, int, long, float, double, {@link java.math.BigDecimal}, or a
     * {@link java.math.BigInteger} that fits in 64 bits.
     *
     * @param document the top-level entries
     * @param indent spaces per nesting level; 0 writes nested values on one line
     * @return the text
     * @throws PhlException if a value cannot be written
     */
    public static String stringify(Map<String, ?> document, int indent) {
        return new Writer(indent).document(document);
    }

    /**
     * Writes an HPL table with 4 spaces of indent.
     *
     * @param rows maps that all have the same keys in the same order
     * @return the text
     * @throws PhlException if the rows are not a table or a value cannot be written
     */
    public static String stringifyTable(Iterable<? extends Map<String, ?>> rows) {
        return stringifyTable(rows, DEFAULT_INDENT);
    }

    /**
     * Writes an HPL table.
     *
     * @param rows maps that all have the same keys in the same order
     * @param indent spaces per nesting level; 0 writes the table on one line
     * @return the text
     * @throws PhlException if the rows are not a table or a value cannot be written
     */
    public static String stringifyTable(Iterable<? extends Map<String, ?>> rows, int indent) {
        return new Writer(indent).tableDocument(rows);
    }

    private static String readAll(Reader reader) throws IOException {
        StringBuilder out = new StringBuilder();
        char[] buffer = new char[8192];
        int count;
        while ((count = reader.read(buffer)) != -1) {
            out.append(buffer, 0, count);
        }
        return out.toString();
    }
}
