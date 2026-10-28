/** Thrown when text is not valid PHL or HPL, or a value cannot be written. */
export class PHLError extends Error {
  constructor(message, line = null, column = null) {
    super(line === null ? message : `${message} (line ${line}, column ${column})`);
    this.name = "PHLError";
    this.reason = message;
    this.line = line;
    this.column = column;
  }
}
