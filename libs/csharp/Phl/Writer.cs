using System;
using System.Collections;
using System.Collections.Generic;
using System.Globalization;
using System.Numerics;
using System.Text;

namespace Phl
{
    /// <summary>Writes .NET values as PHL and HPL text.</summary>
    internal sealed class Writer
    {
        private readonly int _indent;
        private readonly StringBuilder _out = new StringBuilder();
        private int _nesting;

        public Writer(int indent)
        {
            _indent = Math.Max(indent, 0);
        }

        public string Document(object? document)
        {
            List<KeyValuePair<object, object?>>? entries = EntriesOf(document);
            if (entries == null) throw new PhlException("a PHL document must be a dictionary of top-level entries");
            foreach (var entry in entries)
            {
                Key(entry.Key);
                _out.Append(": ");
                Value(entry.Value, 0, false);
                _out.Append('\n');
            }
            return _out.ToString();
        }

        public string TableDocument(object? rows)
        {
            List<object?>? items = rows is string ? null : ItemsOf(rows);
            if (items == null || !TryTable(items, out var table))
            {
                throw new PhlException("an HPL table must be a non-empty list of dictionaries that share the same keys");
            }
            _nesting = 1;
            WriteTable(table, 0, false);
            _out.Append('\n');
            return _out.ToString();
        }

        /// <summary>The entries of a dictionary in order, or null for anything else.</summary>
        private static List<KeyValuePair<object, object?>>? EntriesOf(object? value)
        {
            if (value is IEnumerable<KeyValuePair<string, object?>> generic)
            {
                var entries = new List<KeyValuePair<object, object?>>();
                foreach (var entry in generic) entries.Add(new KeyValuePair<object, object?>(entry.Key, entry.Value));
                return entries;
            }
            if (value is IDictionary dictionary)
            {
                var entries = new List<KeyValuePair<object, object?>>(dictionary.Count);
                foreach (DictionaryEntry entry in dictionary) entries.Add(new KeyValuePair<object, object?>(entry.Key, entry.Value));
                return entries;
            }
            return null;
        }

        /// <summary>The items of a sequence, or null for anything else.</summary>
        private static List<object?>? ItemsOf(object? value)
        {
            if (value is IEnumerable sequence)
            {
                var items = new List<object?>();
                foreach (object? item in sequence) items.Add(item);
                return items;
            }
            return null;
        }

        private static bool IsDictionary(object? value) =>
            value is IDictionary || value is IEnumerable<KeyValuePair<string, object?>>;

        private static bool IsScalar(object? value) =>
            value == null || value is string || !(value is IEnumerable);

        private void Pad(int depth) => _out.Append(' ', _indent * depth);

        private void WriteString(string text)
        {
            _out.Append('"');
            foreach (char c in text)
            {
                switch (c)
                {
                    case '\\': _out.Append("\\\\"); break;
                    case '"': _out.Append("\\\""); break;
                    case '\n': _out.Append("\\n"); break;
                    case '\t': _out.Append("\\t"); break;
                    default:
                        if (c < 0x20) throw new PhlException($"strings cannot contain the control character {(int)c}");
                        _out.Append(c);
                        break;
                }
            }
            _out.Append('"');
        }

        private void Key(object? name)
        {
            if (!(name is string text)) throw new PhlException($"keys must be strings, not {name?.GetType().Name ?? "null"}");
            bool bare = text.Length > 0;
            foreach (char c in text)
            {
                if (!Lexer.IsWordChar(c)) bare = false;
            }
            if (bare)
            {
                _out.Append(text);
            }
            else
            {
                WriteString(text);
            }
        }

        /// <summary>
        /// Plain digits from a number that may be in exponent notation, which PHL
        /// does not support, always with a decimal point.
        /// </summary>
        private void PlainDecimal(string text)
        {
            int e = text.IndexOf('E');
            if (e < 0)
            {
                _out.Append(text);
                if (text.IndexOf('.') < 0) _out.Append(".0");
                return;
            }
            int start = 0;
            if (text[0] == '-')
            {
                _out.Append('-');
                start = 1;
            }
            string mantissa = text.Substring(start, e - start);
            int exponent = int.Parse(text.Substring(e + 1), NumberStyles.AllowLeadingSign, CultureInfo.InvariantCulture);
            int dot = mantissa.IndexOf('.');
            string digits = dot < 0 ? mantissa : mantissa.Remove(dot, 1);
            int point = (dot < 0 ? mantissa.Length : dot) + exponent;
            if (point <= 0)
            {
                _out.Append("0.").Append('0', -point).Append(digits);
            }
            else if (point >= digits.Length)
            {
                _out.Append(digits).Append('0', point - digits.Length).Append(".0");
            }
            else
            {
                _out.Append(digits, 0, point).Append('.').Append(digits, point, digits.Length - point);
            }
        }

        private void WriteDouble(double number)
        {
            if (double.IsNaN(number) || double.IsInfinity(number))
            {
                throw new PhlException($"{number.ToString(CultureInfo.InvariantCulture)} cannot be written; PHL numbers must be finite");
            }
            // "R" can be one digit short on .NET Framework, which G17 corrects.
            string text = number.ToString("R", CultureInfo.InvariantCulture);
            if (double.Parse(text, NumberStyles.Float, CultureInfo.InvariantCulture) != number)
            {
                text = number.ToString("G17", CultureInfo.InvariantCulture);
            }
            PlainDecimal(text);
        }

        private void WriteFloat(float number)
        {
            if (float.IsNaN(number) || float.IsInfinity(number))
            {
                throw new PhlException($"{number.ToString(CultureInfo.InvariantCulture)} cannot be written; PHL numbers must be finite");
            }
            // The float's own digits, not the longer ones of the same value as a double.
            string text = number.ToString("R", CultureInfo.InvariantCulture);
            if (float.Parse(text, NumberStyles.Float, CultureInfo.InvariantCulture) != number)
            {
                text = number.ToString("G9", CultureInfo.InvariantCulture);
            }
            PlainDecimal(text);
        }

        private bool TryScalar(object? value)
        {
            switch (value)
            {
                // PHL has no boolean or null values, so they are written as the
                // strings that coercion reads back.
                case null: _out.Append("\"nil\""); return true;
                case bool b: _out.Append(b ? "\"true\"" : "\"false\""); return true;
                case string s: WriteString(s); return true;
                case long n: _out.Append(n.ToString(CultureInfo.InvariantCulture)); return true;
                case int n: _out.Append(n.ToString(CultureInfo.InvariantCulture)); return true;
                case short n: _out.Append(n.ToString(CultureInfo.InvariantCulture)); return true;
                case sbyte n: _out.Append(n.ToString(CultureInfo.InvariantCulture)); return true;
                case byte n: _out.Append(n.ToString(CultureInfo.InvariantCulture)); return true;
                case ushort n: _out.Append(n.ToString(CultureInfo.InvariantCulture)); return true;
                case uint n: _out.Append(n.ToString(CultureInfo.InvariantCulture)); return true;
                case ulong n:
                    if (n > long.MaxValue) throw new PhlException($"the number {n} does not fit in 64 bits");
                    _out.Append(n.ToString(CultureInfo.InvariantCulture));
                    return true;
                case BigInteger n:
                    if (n > long.MaxValue || n < long.MinValue) throw new PhlException($"the number {n} does not fit in 64 bits");
                    _out.Append(n.ToString(CultureInfo.InvariantCulture));
                    return true;
                case double n: WriteDouble(n); return true;
                case float n: WriteFloat(n); return true;
                case decimal n: PlainDecimal(n.ToString(CultureInfo.InvariantCulture)); return true;
                default: return false;
            }
        }

        private void Value(object? value, int depth, bool inline)
        {
            if (TryScalar(value)) return;

            // The limit also stops values that contain themselves.
            if (_nesting >= PhlConvert.MaxDepth)
            {
                throw new PhlException($"nesting is deeper than the limit of {PhlConvert.MaxDepth} levels");
            }
            List<KeyValuePair<object, object?>>? entries = EntriesOf(value);
            List<object?>? items = entries == null ? ItemsOf(value) : null;
            if (entries == null && items == null)
            {
                throw new PhlException($"cannot write a value of type {value!.GetType().Name}");
            }
            _nesting++;
            if (entries != null)
            {
                WriteObject(entries, depth, inline);
            }
            else if (TryTable(items!, out var table))
            {
                WriteTable(table, depth, inline);
            }
            else
            {
                WriteList(items!, depth, inline);
            }
            _nesting--;
        }

        /// <summary>A non-empty list of dictionaries that all have the same keys in the same order.</summary>
        private static bool TryTable(List<object?> items, out List<List<KeyValuePair<object, object?>>> table)
        {
            table = new List<List<KeyValuePair<object, object?>>>();
            if (items.Count == 0) return false;
            foreach (object? item in items)
            {
                if (!IsDictionary(item)) return false;
            }
            foreach (object? item in items)
            {
                List<KeyValuePair<object, object?>> row = EntriesOf(item)!;
                if (table.Count == 0)
                {
                    if (row.Count == 0) return false;
                }
                else
                {
                    List<KeyValuePair<object, object?>> first = table[0];
                    if (row.Count != first.Count) return false;
                    for (int i = 0; i < row.Count; i++)
                    {
                        if (!Equals(row[i].Key, first[i].Key)) return false;
                    }
                }
                table.Add(row);
            }
            return true;
        }

        private void WriteObject(List<KeyValuePair<object, object?>> entries, int depth, bool inline)
        {
            if (entries.Count == 0)
            {
                _out.Append("{}");
                return;
            }
            bool flat = inline || _indent == 0;
            _out.Append(flat ? "{" : "{\n");
            for (int i = 0; i < entries.Count; i++)
            {
                if (i > 0) _out.Append(flat ? ", " : ",\n");
                if (!flat) Pad(depth + 1);
                Key(entries[i].Key);
                _out.Append(": ");
                Value(entries[i].Value, depth + 1, inline);
            }
            if (!flat)
            {
                _out.Append('\n');
                Pad(depth);
            }
            _out.Append('}');
        }

        private void WriteList(List<object?> items, int depth, bool inline)
        {
            if (items.Count == 0)
            {
                _out.Append("[]");
                return;
            }
            bool oneLine = inline;
            if (!oneLine)
            {
                oneLine = true;
                foreach (object? item in items)
                {
                    if (!IsScalar(item)) oneLine = false;
                }
            }
            bool flat = oneLine || _indent == 0;
            _out.Append(flat ? "[" : "[\n");
            for (int i = 0; i < items.Count; i++)
            {
                if (i > 0) _out.Append(flat ? ", " : ",\n");
                if (!flat) Pad(depth + 1);
                Value(items[i], depth + 1, oneLine);
            }
            if (!flat)
            {
                _out.Append('\n');
                Pad(depth);
            }
            _out.Append(']');
        }

        private void WriteTable(List<List<KeyValuePair<object, object?>>> rows, int depth, bool inline)
        {
            bool flat = inline || _indent == 0;
            _out.Append('(');
            for (int i = 0; i < rows[0].Count; i++)
            {
                if (i > 0) _out.Append(", ");
                Key(rows[0][i].Key);
            }
            _out.Append(flat ? ") { " : ") {\n");
            for (int r = 0; r < rows.Count; r++)
            {
                if (flat)
                {
                    if (r > 0) _out.Append(' ');
                }
                else
                {
                    Pad(depth + 1);
                }
                for (int c = 0; c < rows[r].Count; c++)
                {
                    if (c > 0) _out.Append(", ");
                    Value(rows[r][c].Value, 0, true);
                }
                _out.Append(';');
                if (!flat) _out.Append('\n');
            }
            if (flat)
            {
                _out.Append(" }");
            }
            else
            {
                Pad(depth);
                _out.Append('}');
            }
        }
    }
}
