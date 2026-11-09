<?php

declare(strict_types=1);

namespace Phl;

/**
 * Reads and writes PHL documents (.phl, .pll) and HPL tables (.hpl, .hpll).
 *
 * Objects are read as associative arrays, as with json_decode($text, true), and
 * as stdClass with objects: true. Whole numbers beyond PHP's int are an error.
 */
final class Phl
{
    /** Lists, objects and tables nested deeper than this are an error. */
    public const MAX_DEPTH = 256;

    private function __construct()
    {
    }

    /**
     * Reads a PHL document (UTF-8).
     *
     * @param bool $coerce  Convert the quoted strings "true" and "false" to booleans
     *                      and "nil", "null" and "none" to null, ignoring case.
     * @param bool $objects Read objects as stdClass instead of arrays. PHP arrays
     *                      cannot tell {} from [], or {0: "a"} from ["a"].
     *
     * @return array<mixed>|\stdClass
     *
     * @throws PhlException if the text is not valid PHL
     */
    public static function parse(string $text, bool $coerce = true, bool $objects = false): array|\stdClass
    {
        return (new Parser($text, $coerce, $objects))->document();
    }

    /**
     * Reads an HPL table (UTF-8) into a list with one object for each row.
     *
     * @return list<mixed>
     *
     * @throws PhlException if the text is not a valid table
     */
    public static function parseTable(string $text, bool $coerce = true, bool $objects = false): array
    {
        return (new Parser($text, $coerce, $objects))->tableDocument();
    }

    /**
     * Writes a PHL document. A list of objects that all have the same keys in the
     * same order is written as a table, and booleans and null as "true", "false"
     * and "nil".
     *
     * An array with the keys 0, 1, 2... is a list and any other array is an object.
     * An empty array is an empty list, and a stdClass is always an object.
     *
     * @param array<mixed>|\stdClass $document the top-level entries
     * @param int                    $indent   Spaces per nesting level; 0 writes
     *                                         nested values on one line.
     *
     * @throws PhlException if a value cannot be written
     */
    public static function stringify(array|\stdClass $document, int $indent = 4): string
    {
        return (new Writer($indent))->document($document);
    }

    /**
     * Writes rows that all have the same keys in the same order as an HPL table.
     *
     * @param array<mixed> $rows
     *
     * @throws PhlException if the rows are not a table or a value cannot be written
     */
    public static function stringifyTable(array $rows, int $indent = 4): string
    {
        return (new Writer($indent))->tableDocument($rows);
    }
}
