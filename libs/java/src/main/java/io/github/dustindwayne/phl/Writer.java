package io.github.dustindwayne.phl;

import java.lang.reflect.Array;
import java.math.BigDecimal;
import java.math.BigInteger;
import java.util.ArrayList;
import java.util.Iterator;
import java.util.List;
import java.util.Map;

/** Writes Java values as PHL and HPL text. */
final class Writer {
    private final int indent;
    private final StringBuilder out = new StringBuilder();
    private int nesting = 0;

    Writer(int indent) {
        this.indent = Math.max(indent, 0);
    }

    String document(Map<String, ?> document) {
        if (document == null) {
            throw new PhlException("a PHL document must be a map of top-level entries");
        }
        for (Map.Entry<?, ?> entry : document.entrySet()) {
            key(entry.getKey());
            out.append(": ");
            value(entry.getValue(), 0, false);
            out.append('\n');
        }
        return out.toString();
    }

    String tableDocument(Iterable<? extends Map<String, ?>> rows) {
        List<Object> list = new ArrayList<>();
        if (rows != null) {
            for (Object row : rows) {
                list.add(row);
            }
        }
        if (!isTable(list)) {
            throw new PhlException("an HPL table must be a non-empty list of maps that share the same keys");
        }
        nesting = 1;
        table(list, 0, false);
        out.append('\n');
        return out.toString();
    }

    private void pad(int depth) {
        for (long i = 0, count = (long) indent * depth; i < count; i++) {
            out.append(' ');
        }
    }

    private void string(CharSequence text) {
        out.append('"');
        for (int i = 0; i < text.length(); i++) {
            char c = text.charAt(i);
            switch (c) {
                case '\\':
                    out.append("\\\\");
                    break;
                case '"':
                    out.append("\\\"");
                    break;
                case '\n':
                    out.append("\\n");
                    break;
                case '\t':
                    out.append("\\t");
                    break;
                default:
                    if (c < 0x20) {
                        throw new PhlException("strings cannot contain the control character " + (int) c);
                    }
                    out.append(c);
                    break;
            }
        }
        out.append('"');
    }

    private void key(Object name) {
        if (!(name instanceof String)) {
            throw new PhlException("keys must be strings, not " + typeName(name));
        }
        String text = (String) name;
        boolean bare = !text.isEmpty();
        for (int i = 0; bare && i < text.length(); i++) {
            bare = Lexer.isWordChar(text.charAt(i));
        }
        if (bare) {
            out.append(text);
        } else {
            string(text);
        }
    }

    private static String typeName(Object value) {
        return value == null ? "null" : value.getClass().getName();
    }

    private void writeDouble(double number) {
        if (Double.isNaN(number) || Double.isInfinite(number)) {
            throw new PhlException(number + " cannot be written; PHL numbers must be finite");
        }
        decimalText(Double.toString(number));
    }

    /** No exponent notation, which PHL does not support. */
    private void decimalText(String text) {
        if (text.indexOf('E') < 0) {
            out.append(text);
            return;
        }
        String plain = new BigDecimal(text).stripTrailingZeros().toPlainString();
        out.append(plain);
        if (plain.indexOf('.') < 0) {
            out.append(".0");
        }
    }

    private void number(Number value) {
        if (value instanceof Long || value instanceof Integer || value instanceof Short || value instanceof Byte) {
            out.append(value.longValue());
        } else if (value instanceof Double) {
            writeDouble(value.doubleValue());
        } else if (value instanceof Float) {
            // The float's own digits, not the longer ones of the same value as a double.
            float number = value.floatValue();
            if (Float.isNaN(number) || Float.isInfinite(number)) {
                throw new PhlException(number + " cannot be written; PHL numbers must be finite");
            }
            decimalText(Float.toString(number));
        } else if (value instanceof BigInteger) {
            BigInteger number = (BigInteger) value;
            if (number.bitLength() > 63) {
                throw new PhlException("the number " + number + " does not fit in 64 bits");
            }
            out.append(number);
        } else if (value instanceof BigDecimal) {
            String plain = ((BigDecimal) value).toPlainString();
            out.append(plain);
            if (plain.indexOf('.') < 0) {
                out.append(".0");
            }
        } else {
            throw new PhlException("cannot write a value of type " + typeName(value));
        }
    }

    /** The items of an Iterable or array, or null for anything else. */
    private static List<Object> itemsOf(Object value) {
        List<Object> items;
        if (value instanceof Iterable) {
            items = new ArrayList<>();
            for (Object item : (Iterable<?>) value) {
                items.add(item);
            }
            return items;
        }
        if (value != null && value.getClass().isArray()) {
            int length = Array.getLength(value);
            items = new ArrayList<>(length);
            for (int i = 0; i < length; i++) {
                items.add(Array.get(value, i));
            }
            return items;
        }
        return null;
    }

    private static boolean isScalar(Object value) {
        return !(value instanceof Map)
                && !(value instanceof Iterable)
                && (value == null || !value.getClass().isArray());
    }

    /** A non-empty list of maps that all have the same keys in the same order. */
    private static boolean isTable(List<Object> items) {
        if (items.isEmpty()) {
            return false;
        }
        for (Object item : items) {
            if (!(item instanceof Map)) {
                return false;
            }
        }
        Map<?, ?> first = (Map<?, ?>) items.get(0);
        if (first.isEmpty()) {
            return false;
        }
        for (int i = 1; i < items.size(); i++) {
            Map<?, ?> row = (Map<?, ?>) items.get(i);
            if (row.size() != first.size()) {
                return false;
            }
            Iterator<?> a = first.keySet().iterator();
            Iterator<?> b = row.keySet().iterator();
            while (a.hasNext()) {
                Object x = a.next();
                if (!b.hasNext() || !java.util.Objects.equals(x, b.next())) {
                    return false;
                }
            }
        }
        return true;
    }

    private void value(Object value, int depth, boolean inline) {
        // PHL has no boolean or null values, so they are written as the strings
        // that coercion reads back.
        if (value == null) {
            out.append("\"nil\"");
        } else if (value instanceof Boolean) {
            out.append((Boolean) value ? "\"true\"" : "\"false\"");
        } else if (value instanceof Number) {
            number((Number) value);
        } else if (value instanceof CharSequence) {
            string((CharSequence) value);
        } else if (value instanceof Map) {
            enter();
            object((Map<?, ?>) value, depth, inline);
            nesting--;
        } else {
            List<Object> items = itemsOf(value);
            if (items == null) {
                throw new PhlException("cannot write a value of type " + typeName(value));
            }
            enter();
            if (isTable(items)) {
                table(items, depth, inline);
            } else {
                list(items, depth, inline);
            }
            nesting--;
        }
    }

    /** This also stops values that contain themselves. */
    private void enter() {
        if (nesting >= Phl.MAX_DEPTH) {
            throw new PhlException("nesting is deeper than the limit of " + Phl.MAX_DEPTH + " levels");
        }
        nesting++;
    }

    private void object(Map<?, ?> value, int depth, boolean inline) {
        if (value.isEmpty()) {
            out.append("{}");
            return;
        }
        boolean flat = inline || indent == 0;
        out.append(flat ? "{" : "{\n");
        boolean first = true;
        for (Map.Entry<?, ?> entry : value.entrySet()) {
            if (!first) {
                out.append(flat ? ", " : ",\n");
            }
            first = false;
            if (!flat) {
                pad(depth + 1);
            }
            key(entry.getKey());
            out.append(": ");
            value(entry.getValue(), depth + 1, inline);
        }
        if (!flat) {
            out.append('\n');
            pad(depth);
        }
        out.append('}');
    }

    private void list(List<Object> items, int depth, boolean inline) {
        if (items.isEmpty()) {
            out.append("[]");
            return;
        }
        boolean oneLine = inline;
        if (!oneLine) {
            oneLine = true;
            for (Object item : items) {
                if (!isScalar(item)) {
                    oneLine = false;
                    break;
                }
            }
        }
        boolean flat = oneLine || indent == 0;
        out.append(flat ? "[" : "[\n");
        boolean first = true;
        for (Object item : items) {
            if (!first) {
                out.append(flat ? ", " : ",\n");
            }
            first = false;
            if (!flat) {
                pad(depth + 1);
            }
            value(item, depth + 1, oneLine);
        }
        if (!flat) {
            out.append('\n');
            pad(depth);
        }
        out.append(']');
    }

    private void table(List<Object> rows, int depth, boolean inline) {
        boolean flat = inline || indent == 0;
        out.append('(');
        boolean first = true;
        for (Object column : ((Map<?, ?>) rows.get(0)).keySet()) {
            if (!first) {
                out.append(", ");
            }
            first = false;
            key(column);
        }
        out.append(flat ? ") { " : ") {\n");
        first = true;
        for (Object row : rows) {
            if (flat) {
                if (!first) {
                    out.append(' ');
                }
                first = false;
            } else {
                pad(depth + 1);
            }
            boolean firstCell = true;
            for (Object cell : ((Map<?, ?>) row).values()) {
                if (!firstCell) {
                    out.append(", ");
                }
                firstCell = false;
                value(cell, 0, true);
            }
            out.append(';');
            if (!flat) {
                out.append('\n');
            }
        }
        if (flat) {
            out.append(" }");
        } else {
            pad(depth);
            out.append('}');
        }
    }
}
