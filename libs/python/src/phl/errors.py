class PHLError(ValueError):
    """Raised when text is not valid PHL or HPL, or a value cannot be written."""

    def __init__(self, message, line=None, column=None):
        self.message = message
        self.line = line
        self.column = column
        if line is not None:
            message = f"{message} (line {line}, column {column})"
        super().__init__(message)
