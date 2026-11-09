package io.github.dustindwayne.phl;

/**
 * Thrown for text that is not valid PHL or HPL, and for values that cannot be
 * written.
 */
public class PhlException extends RuntimeException {
    private static final long serialVersionUID = 1L;

    private final String reason;
    private final int line;
    private final int column;

    PhlException(String reason) {
        this(reason, 0, 0);
    }

    PhlException(String reason, int line, int column) {
        super(line > 0 ? reason + " (line " + line + ", column " + column + ")" : reason);
        this.reason = reason;
        this.line = line;
        this.column = column;
    }

    /**
     * The message without the position.
     *
     * @return the message without the position
     */
    public String getReason() {
        return reason;
    }

    /**
     * The line of the error, counted from 1.
     *
     * @return the line, or 0 when the error has no position
     */
    public int getLine() {
        return line;
    }

    /**
     * The column of the error, counted from 1 in Unicode code points.
     *
     * @return the column, or 0 when the error has no position
     */
    public int getColumn() {
        return column;
    }
}
