using System;
using System.Collections.Generic;
using System.Globalization;

namespace Phl
{
    /// <summary>Builds .NET values from PHL and HPL tokens.</summary>
    internal sealed class Parser
    {
        private readonly Lexer _lexer;
        private readonly List<Token> _tokens;
        private readonly bool _coerce;
        private int _pos;
        private int _depth;

        public Parser(string text, bool coerce)
        {
            _lexer = new Lexer(text);
            _tokens = _lexer.Run();
            _coerce = coerce;
        }

        private Token Peek(int offset = 0) => _tokens[Math.Min(_pos + offset, _tokens.Count - 1)];

        private Token Next()
        {
            Token token = _tokens[_pos];
            if (token.Kind != TokenKind.End) _pos++;
            return token;
        }

        private void SkipNewlines()
        {
            while (Peek().Kind == TokenKind.Newline) _pos++;
        }

        private PhlException Error(string message, Token token) =>
            new PhlException(message, token.Line, _lexer.ColumnOf(token.LineStart, token.Start));

        private bool IsKeyAhead()
        {
            if (!Peek().IsKeyLike) return false;
            int offset = 1;
            while (Peek(offset).Kind == TokenKind.Newline) offset++;
            return Peek(offset).IsPunct(':');
        }

        private void Enter(Token open)
        {
            if (_depth >= PhlConvert.MaxDepth)
            {
                throw Error($"nesting is deeper than the limit of {PhlConvert.MaxDepth} levels", open);
            }
            _depth++;
        }

        // Documents

        public PhlObject Document()
        {
            var result = new PhlObject();
            SkipNewlines();
            while (Peek().Kind != TokenKind.End)
            {
                Entry(result);
                Token token = Peek();
                if (token.IsPunct(','))
                {
                    Next();
                    SkipNewlines();
                }
                else if (token.Kind == TokenKind.Newline)
                {
                    SkipNewlines();
                }
                else if (token.Kind != TokenKind.End)
                {
                    throw Error($"expected a new line or ',' between entries, found {token.Describe()}", token);
                }
            }
            return result;
        }

        public List<PhlObject> TableDocument()
        {
            SkipNewlines();
            Token first = Peek();
            if (!first.IsPunct('('))
            {
                throw Error($"expected '(' to start the column list, found {first.Describe()}", first);
            }
            List<PhlObject> rows = Rows();
            SkipNewlines();
            Token token = Peek();
            if (token.Kind != TokenKind.End)
            {
                throw Error($"unexpected {token.Describe()} after the table", token);
            }
            return rows;
        }

        // Entries and values

        private void Entry(PhlObject target)
        {
            Token keyToken = Next();
            if (!keyToken.IsKeyLike) throw Error($"expected a key, found {keyToken.Describe()}", keyToken);
            string key = keyToken.Text;
            SkipNewlines();
            Token colon = Next();
            if (!colon.IsPunct(':'))
            {
                throw Error($"expected ':' after key '{Lexer.Shorten(key)}', found {colon.Describe()}", colon);
            }
            SkipNewlines();
            if (target.ContainsKey(key)) throw Error($"duplicate key '{Lexer.Shorten(key)}'", keyToken);
            target[key] = Value();
        }

        private static bool EqualsAsciiIgnoreCase(string text, string lower)
        {
            if (text.Length != lower.Length) return false;
            for (int i = 0; i < text.Length; i++)
            {
                char c = text[i];
                if (c >= 'A' && c <= 'Z') c = (char)(c - 'A' + 'a');
                if (c != lower[i]) return false;
            }
            return true;
        }

        private object? StringValue(Token token)
        {
            string text = token.Text;
            if (_coerce)
            {
                if (EqualsAsciiIgnoreCase(text, "true")) return true;
                if (EqualsAsciiIgnoreCase(text, "false")) return false;
                if (EqualsAsciiIgnoreCase(text, "nil") || EqualsAsciiIgnoreCase(text, "null") || EqualsAsciiIgnoreCase(text, "none"))
                {
                    return null;
                }
            }
            return text;
        }

        private object IntegerValue(Token token)
        {
            if (long.TryParse(token.Text, NumberStyles.AllowLeadingSign, CultureInfo.InvariantCulture, out long number))
            {
                return number;
            }
            string shown = token.Text.Length > 40 ? token.Text.Substring(0, 40) + "..." : token.Text;
            throw Error($"the number {shown} does not fit in 64 bits", token);
        }

        private object DecimalValue(Token token)
        {
            double number;
            try
            {
                number = double.Parse(token.Text, NumberStyles.Float, CultureInfo.InvariantCulture);
            }
            catch (OverflowException)
            {
                // .NET Framework throws where newer versions give infinity.
                number = double.PositiveInfinity;
            }
            if (double.IsInfinity(number)) throw Error("this number is too large for a 64-bit decimal", token);
            return number;
        }

        private object? Value()
        {
            Token token = Peek();
            switch (token.Kind)
            {
                case TokenKind.String:
                    Next();
                    return StringValue(token);
                case TokenKind.Integer:
                    Next();
                    return IntegerValue(token);
                case TokenKind.Decimal:
                    Next();
                    return DecimalValue(token);
                case TokenKind.Word:
                    Next();
                    if (Lexer.AllDigits(token.Text, 0)) return IntegerValue(token);
                    throw Error($"unexpected word '{Lexer.Shorten(token.Text)}'; strings need double quotes", token);
                case TokenKind.Punct:
                    if (token.IsPunct('{')) return Object();
                    if (token.IsPunct('[')) return ReadList();
                    if (token.IsPunct('('))
                    {
                        // Tables read as an ordinary list of objects.
                        return new List<object?>(Rows());
                    }
                    break;
            }
            throw Error($"expected a value, found {token.Describe()}", token);
        }

        private PhlObject Object()
        {
            Enter(Next());
            var result = new PhlObject();
            while (true)
            {
                SkipNewlines();
                if (Peek().IsPunct('}'))
                {
                    Next();
                    break;
                }
                if (!IsKeyAhead()) throw Error("expected 'key: value' inside an object", Peek());
                Entry(result);
                if (!ItemSeparator("object", '}')) break;
            }
            _depth--;
            return result;
        }

        private List<object?> ReadList()
        {
            Enter(Next());
            var result = new List<object?>();
            while (true)
            {
                SkipNewlines();
                if (Peek().IsPunct(']'))
                {
                    Next();
                    break;
                }
                if (IsKeyAhead())
                {
                    throw Error("a list cannot contain 'key: value' entries; use { } for an object", Peek());
                }
                result.Add(Value());
                if (!ItemSeparator("list", ']')) break;
            }
            _depth--;
            return result;
        }

        /// <summary>Reads ',' (gives true) or the closing bracket (gives false).</summary>
        private bool ItemSeparator(string kind, char closer)
        {
            SkipNewlines();
            Token token = Next();
            if (token.IsPunct(',')) return true;
            if (token.IsPunct(closer)) return false;
            throw Error($"expected ',' or '{closer}' in {kind}, found {token.Describe()}", token);
        }

        // Tables

        /// <summary>(col, col) { value, value; value, value; }</summary>
        private List<PhlObject> Rows()
        {
            Enter(Next());
            List<string> columns = Columns();
            SkipNewlines();
            Token open = Next();
            if (!open.IsPunct('{')) throw Error($"expected '{{' to start the table rows, found {open.Describe()}", open);

            var rows = new List<PhlObject>();
            var cells = new List<object?>();
            while (true)
            {
                SkipNewlines();
                if (Peek().IsPunct('}'))
                {
                    Next();
                    break;
                }
                Token rowToken = Peek();
                cells.Clear();
                while (true)
                {
                    SkipNewlines();
                    cells.Add(Value());
                    SkipNewlines();
                    Token token = Next();
                    if (token.IsPunct(';')) break;
                    if (!token.IsPunct(',')) throw Error($"expected ',' or ';' in table row, found {token.Describe()}", token);
                }
                if (cells.Count != columns.Count)
                {
                    throw Error($"row has {cells.Count} values but the table has {columns.Count} columns", rowToken);
                }
                var row = new PhlObject();
                for (int i = 0; i < columns.Count; i++) row[columns[i]] = cells[i];
                rows.Add(row);
            }
            _depth--;
            return rows;
        }

        private List<string> Columns()
        {
            var columns = new List<string>();
            var seen = new HashSet<string>(StringComparer.Ordinal);
            while (true)
            {
                SkipNewlines();
                if (Peek().IsPunct(')') && columns.Count > 0)
                {
                    Next();
                    return columns;
                }
                Token token = Next();
                if (!token.IsKeyLike) throw Error($"expected a column name, found {token.Describe()}", token);
                if (!seen.Add(token.Text)) throw Error($"duplicate column '{Lexer.Shorten(token.Text)}'", token);
                columns.Add(token.Text);
                SkipNewlines();
                token = Next();
                if (token.IsPunct(')')) return columns;
                if (!token.IsPunct(',')) throw Error($"expected ',' or ')' in column list, found {token.Describe()}", token);
            }
        }
    }
}
