using System;
using System.Collections.Generic;
using System.IO;

namespace Phl
{
    /// <summary>
    /// Reads and writes PHL documents (.phl, .pll) and HPL tables (.hpl, .hpll).
    /// </summary>
    /// <remarks>
    /// Values are read as <see cref="PhlObject"/>, <see cref="List{T}"/> of object,
    /// <see cref="string"/>, <see cref="long"/>, <see cref="double"/>,
    /// <see cref="bool"/> and null. Whole numbers beyond 64 bits are an error.
    /// </remarks>
    public static class PhlConvert
    {
        /// <summary>Lists, objects and tables nested deeper than this are an error.</summary>
        public const int MaxDepth = 256;

        /// <summary>
        /// Reads a PHL document. With <paramref name="coerce"/>, the quoted strings
        /// "true" and "false" become booleans and "nil", "null" and "none" become
        /// null, ignoring case.
        /// </summary>
        /// <exception cref="PhlException">The text is not valid PHL.</exception>
        public static PhlObject Parse(string text, bool coerce = true)
        {
            if (text == null) throw new ArgumentNullException(nameof(text));
            return new Parser(text, coerce).Document();
        }

        /// <summary>Reads a PHL document until the end of the reader, which stays open.</summary>
        /// <exception cref="PhlException">The text is not valid PHL.</exception>
        public static PhlObject Parse(TextReader reader, bool coerce = true)
        {
            if (reader == null) throw new ArgumentNullException(nameof(reader));
            return Parse(reader.ReadToEnd(), coerce);
        }

        /// <summary>Reads an HPL table, with one <see cref="PhlObject"/> for each row.</summary>
        /// <exception cref="PhlException">The text is not a valid table.</exception>
        public static List<PhlObject> ParseTable(string text, bool coerce = true)
        {
            if (text == null) throw new ArgumentNullException(nameof(text));
            return new Parser(text, coerce).TableDocument();
        }

        /// <summary>Reads an HPL table until the end of the reader, which stays open.</summary>
        /// <exception cref="PhlException">The text is not a valid table.</exception>
        public static List<PhlObject> ParseTable(TextReader reader, bool coerce = true)
        {
            if (reader == null) throw new ArgumentNullException(nameof(reader));
            return ParseTable(reader.ReadToEnd(), coerce);
        }

        /// <summary>
        /// Writes a PHL document. A list of objects that all have the same keys in
        /// the same order is written as a table, and booleans and null as "true",
        /// "false" and "nil".
        /// </summary>
        /// <remarks>
        /// The document is a dictionary with string keys. Values can be a dictionary,
        /// any other <see cref="System.Collections.IEnumerable"/>, a string, a bool,
        /// null, or a number. Numbers must fit in 64 bits. Only
        /// <see cref="PhlObject"/> promises to keep the order of its keys.
        /// </remarks>
        /// <param name="document">The top-level entries.</param>
        /// <param name="indent">Spaces per nesting level; 0 writes nested values on one line.</param>
        /// <exception cref="PhlException">A value cannot be written.</exception>
        public static string Stringify(object document, int indent = 4)
        {
            return new Writer(indent).Document(document);
        }

        /// <summary>Writes rows that all have the same keys in the same order as an HPL table.</summary>
        /// <param name="rows">The rows, each a dictionary with string keys.</param>
        /// <param name="indent">Spaces per nesting level; 0 writes the table on one line.</param>
        /// <exception cref="PhlException">The rows are not a table, or a value cannot be written.</exception>
        public static string StringifyTable(object rows, int indent = 4)
        {
            return new Writer(indent).TableDocument(rows);
        }
    }
}
