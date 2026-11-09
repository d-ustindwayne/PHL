using System.Collections.Generic;
using System.Text;

namespace Phl
{
    internal enum TokenKind
    {
        String,
        Integer, // a negative whole number; plain digits are Word tokens
        Decimal,
        Word,
        Punct,
        Newline,
        End,
    }

    internal readonly struct Token
    {
        public readonly TokenKind Kind;
        public readonly string Text;
        public readonly int Line;
        public readonly int LineStart;
        public readonly int Start;

        public Token(TokenKind kind, string text, int line, int lineStart, int start)
        {
            Kind = kind;
            Text = text;
            Line = line;
            LineStart = lineStart;
            Start = start;
        }

        public bool IsPunct(char c) => Kind == TokenKind.Punct && Text[0] == c;

        public bool IsKeyLike => Kind == TokenKind.Word || Kind == TokenKind.String;

        public string Describe()
        {
            switch (Kind)
            {
                case TokenKind.End: return "end of input";
                case TokenKind.Newline: return "end of line";
                case TokenKind.String: return "string";
                default: return $"'{Text}'";
            }
        }
    }

    /// <summary>Turns PHL and HPL text into tokens.</summary>
    internal sealed class Lexer
    {
        private readonly string _text;
        private readonly List<Token> _tokens = new List<Token>();
        private int _pos;
        private int _line = 1;
        private int _lineStart;

        public Lexer(string text)
        {
            _text = text;
        }

        public static bool IsDigit(char c) => c >= '0' && c <= '9';

        public static bool IsWordChar(char c) =>
            IsDigit(c) || (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || c == '_';

        public static bool AllDigits(string s, int from)
        {
            if (from >= s.Length) return false;
            for (int i = from; i < s.Length; i++)
            {
                if (!IsDigit(s[i])) return false;
            }
            return true;
        }

        /// <summary>Columns count code points, so a surrogate pair is one column.</summary>
        public int ColumnOf(int lineStart, int offset)
        {
            int column = 1;
            for (int i = lineStart; i < offset; i++)
            {
                if (char.IsLowSurrogate(_text[i]) && i > lineStart && char.IsHighSurrogate(_text[i - 1])) continue;
                column++;
            }
            return column;
        }

        public static string Shorten(string s) => s.Length > 64 ? s.Substring(0, 64) : s;

        private PhlException Error(string message, int offset) =>
            new PhlException(message, _line, ColumnOf(_lineStart, offset));

        private void Add(TokenKind kind, string value, int start) =>
            _tokens.Add(new Token(kind, value, _line, _lineStart, start));

        /// <summary>The text of the whole character at an offset.</summary>
        private string CharacterAt(int offset)
        {
            bool pair = char.IsHighSurrogate(_text[offset]) && offset + 1 < _text.Length && char.IsLowSurrogate(_text[offset + 1]);
            return _text.Substring(offset, pair ? 2 : 1);
        }

        public List<Token> Run()
        {
            int length = _text.Length;
            if (length > 0 && _text[0] == '﻿')
            {
                _pos = 1;
                _lineStart = 1;
            }
            while (_pos < length)
            {
                char c = _text[_pos];
                if (c == '\n')
                {
                    Add(TokenKind.Newline, "\n", _pos);
                    _pos++;
                    _line++;
                    _lineStart = _pos;
                }
                else if (c == ' ' || c == '\t' || c == '\r')
                {
                    _pos++;
                }
                else if (c == '/' && _pos + 1 < length && _text[_pos + 1] == '/')
                {
                    int end = _text.IndexOf('\n', _pos);
                    _pos = end < 0 ? length : end;
                }
                else if (c == '"')
                {
                    ReadString();
                }
                else if (c == '-' || IsDigit(c))
                {
                    ReadNumberOrWord();
                }
                else if (IsWordChar(c))
                {
                    int start = _pos;
                    while (_pos < length && IsWordChar(_text[_pos])) _pos++;
                    Add(TokenKind.Word, _text.Substring(start, _pos - start), start);
                }
                else if (":,;[]{}()".IndexOf(c) >= 0)
                {
                    Add(TokenKind.Punct, c.ToString(), _pos);
                    _pos++;
                }
                else if (c < 0x20)
                {
                    throw Error($"unexpected control character {(int)c}", _pos);
                }
                else
                {
                    throw Error($"unexpected character '{CharacterAt(_pos)}'", _pos);
                }
            }
            Add(TokenKind.End, "", length);
            return _tokens;
        }

        private void ReadString()
        {
            int length = _text.Length;
            int start = _pos;
            int i = start + 1;
            int runStart = i;
            StringBuilder? builder = null;
            while (true)
            {
                if (i >= length || _text[i] == '\n') throw Error("unterminated string", start);
                char c = _text[i];
                if (c == '"')
                {
                    string value = builder == null
                        ? _text.Substring(runStart, i - runStart)
                        : builder.Append(_text, runStart, i - runStart).ToString();
                    Add(TokenKind.String, value, start);
                    _pos = i + 1;
                    return;
                }
                if (c == '\\')
                {
                    char decoded;
                    switch (i + 1 < length ? _text[i + 1] : '?')
                    {
                        case 'n': decoded = '\n'; break;
                        case 't': decoded = '\t'; break;
                        case '"': decoded = '"'; break;
                        case '\\': decoded = '\\'; break;
                        default:
                            string shown = i + 1 < length && _text[i + 1] != '\n' ? CharacterAt(i + 1) : "";
                            throw Error($"invalid escape sequence '\\{shown}'", i);
                    }
                    builder ??= new StringBuilder();
                    builder.Append(_text, runStart, i - runStart).Append(decoded);
                    i += 2;
                    runStart = i;
                }
                else
                {
                    i++;
                }
            }
        }

        private void ReadNumberOrWord()
        {
            int length = _text.Length;
            int start = _pos;
            int i = start;
            bool negative = _text[i] == '-';
            if (negative)
            {
                i++;
                if (i >= length || !IsDigit(_text[i])) throw Error("expected a digit after '-'", start);
            }
            while (i < length && IsWordChar(_text[i])) i++;
            string head = _text.Substring(start, i - start);
            if (!AllDigits(head, negative ? 1 : 0))
            {
                if (negative) throw Error($"invalid number '{Shorten(head)}'", start);
                // Keys may start with a digit, for example 2fa_enabled.
                Add(TokenKind.Word, head, start);
                _pos = i;
                return;
            }
            if (i + 1 < length && _text[i] == '.' && IsDigit(_text[i + 1]))
            {
                i++;
                while (i < length && IsDigit(_text[i])) i++;
                if (i < length && IsWordChar(_text[i]))
                {
                    throw Error($"invalid number '{Shorten(_text.Substring(start, i + 1 - start))}'", start);
                }
                Add(TokenKind.Decimal, _text.Substring(start, i - start), start);
                _pos = i;
                return;
            }
            // A plain run of digits can be a key or a number; the parser decides.
            Add(negative ? TokenKind.Integer : TokenKind.Word, head, start);
            _pos = i;
        }
    }
}
