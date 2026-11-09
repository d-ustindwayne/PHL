<?php

declare(strict_types=1);

namespace Phl;

/**
 * Thrown for text that is not valid PHL or HPL, and for values that cannot be
 * written.
 */
class PhlException extends \RuntimeException
{
    /** The message without the position. */
    public readonly string $reason;

    /** The line of the error, counted from 1, or 0 when it has no position. */
    public readonly int $errorLine;

    /** The column, counted from 1 in Unicode code points, or 0 when it has no position. */
    public readonly int $errorColumn;

    public function __construct(string $reason, int $line = 0, int $column = 0)
    {
        parent::__construct($line > 0 ? "{$reason} (line {$line}, column {$column})" : $reason);
        $this->reason = $reason;
        $this->errorLine = $line;
        $this->errorColumn = $column;
    }
}
