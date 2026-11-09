using System;

namespace Phl
{
    /// <summary>
    /// Thrown for text that is not valid PHL or HPL, and for values that cannot be
    /// written.
    /// </summary>
    public class PhlException : Exception
    {
        /// <summary>The message without the position.</summary>
        public string Reason { get; }

        /// <summary>The line of the error, counted from 1, or 0 when it has no position.</summary>
        public int Line { get; }

        /// <summary>
        /// The column of the error, counted from 1 in Unicode code points, or 0 when
        /// it has no position.
        /// </summary>
        public int Column { get; }

        internal PhlException(string reason, int line = 0, int column = 0)
            : base(line > 0 ? $"{reason} (line {line}, column {column})" : reason)
        {
            Reason = reason;
            Line = line;
            Column = column;
        }
    }
}
