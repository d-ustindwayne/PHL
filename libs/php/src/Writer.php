<?php

declare(strict_types=1);

namespace Phl;

/**
 * Writes PHP values as PHL and HPL text.
 *
 * @internal
 */
final class Writer
{
    // Everything below space, except the line feed and tab that have escapes.
    private const CONTROL_CHARACTERS = "\x00\x01\x02\x03\x04\x05\x06\x07\x08\x0B\x0C\x0D\x0E\x0F"
        . "\x10\x11\x12\x13\x14\x15\x16\x17\x18\x19\x1A\x1B\x1C\x1D\x1E\x1F";

    private readonly string $indent;
    private int $nesting = 0;

    public function __construct(int $indent)
    {
        $this->indent = $indent > 0 ? str_repeat(' ', $indent) : '';
    }

    /** @param array<mixed>|\stdClass $document */
    public function document(array|\stdClass $document): string
    {
        if (is_array($document) && $document !== [] && array_is_list($document)) {
            throw new PhlException(
                'a PHL document must be an object of top-level entries, but this array is a list; '
                . 'read documents with objects: true to keep keys such as 0',
            );
        }
        $out = '';
        foreach ($document as $key => $value) {
            $out .= self::key((string) $key) . ': ' . $this->value($value, 0, false) . "\n";
        }
        return $out;
    }

    /** @param array<mixed> $rows */
    public function tableDocument(array $rows): string
    {
        if (!self::isTable($rows)) {
            throw new PhlException('an HPL table must be a non-empty list of objects that share the same keys');
        }
        $this->nesting = 1;
        return $this->table($rows, 0, false) . "\n";
    }

    private static function string(string $text): string
    {
        if (preg_match('//u', $text) !== 1) {
            throw new PhlException('strings must be valid UTF-8');
        }
        $at = strcspn($text, self::CONTROL_CHARACTERS);
        if ($at !== strlen($text)) {
            throw new PhlException('strings cannot contain the control character ' . ord($text[$at]));
        }
        return '"' . strtr($text, ['\\' => '\\\\', '"' => '\\"', "\n" => '\\n', "\t" => '\\t']) . '"';
    }

    private static function key(string $name): string
    {
        return preg_match('/^[A-Za-z0-9_]+$/D', $name) === 1 ? $name : self::string($name);
    }

    /** The shortest digits that read back as the same number, without an exponent. */
    private static function decimal(float $value): string
    {
        if (is_nan($value) || is_infinite($value)) {
            $name = is_nan($value) ? 'nan' : ($value < 0 ? '-inf' : 'inf');
            throw new PhlException("{$name} cannot be written; PHL numbers must be finite");
        }
        if ($value === 0.0) {
            // sprintf loses the sign of -0.0.
            return fdiv(1.0, $value) < 0 ? '-0.0' : '0.0';
        }
        $sign = '';
        $digits = '0';
        $exponent = 0;
        for ($precision = 0; $precision <= 16; $precision++) {
            // %e writes the decimal point of the current locale, which is skipped here.
            $parts = explode('e', strtolower(sprintf("%.{$precision}e", $value)));
            $sign = $parts[0][0] === '-' ? '-' : '';
            $digits = preg_replace('/[^0-9]/', '', $parts[0]) ?? '0';
            $exponent = (int) $parts[1];
            $canonical = "{$sign}{$digits[0]}." . (substr($digits, 1) ?: '0') . "e{$exponent}";
            if ((float) $canonical === $value) {
                break;
            }
        }
        $digits = rtrim($digits, '0');
        if ($digits === '') {
            return "{$sign}0.0";
        }
        if ($exponent < 0) {
            return $sign . '0.' . str_repeat('0', -$exponent - 1) . $digits;
        }
        $whole = $exponent + 1;
        if (strlen($digits) <= $whole) {
            return $sign . $digits . str_repeat('0', $whole - strlen($digits)) . '.0';
        }
        return $sign . substr($digits, 0, $whole) . '.' . substr($digits, $whole);
    }

    private static function isObject(mixed $value): bool
    {
        return $value instanceof \stdClass || (is_array($value) && $value !== [] && !array_is_list($value));
    }

    /**
     * A non-empty list of objects that all have the same keys in the same order.
     *
     * @param array<mixed> $value
     */
    private static function isTable(array $value): bool
    {
        if ($value === [] || !array_is_list($value)) {
            return false;
        }
        $columns = null;
        foreach ($value as $row) {
            if (!self::isObject($row)) {
                return false;
            }
            $keys = array_keys((array) $row);
            if ($columns === null) {
                if ($keys === []) {
                    return false;
                }
                $columns = $keys;
            } elseif ($keys !== $columns) {
                return false;
            }
        }
        return true;
    }

    private static function isScalar(mixed $value): bool
    {
        return !is_array($value) && !is_object($value);
    }

    private function value(mixed $value, int $depth, bool $inline): string
    {
        // PHL has no boolean or null values, so they are written as the strings
        // that coercion reads back.
        if ($value === null) {
            return '"nil"';
        }
        if (is_bool($value)) {
            return $value ? '"true"' : '"false"';
        }
        if (is_int($value)) {
            return (string) $value;
        }
        if (is_float($value)) {
            return self::decimal($value);
        }
        if (is_string($value)) {
            return self::string($value);
        }
        if (!is_array($value) && !($value instanceof \stdClass)) {
            throw new PhlException('cannot write a value of type ' . get_debug_type($value));
        }

        // The limit also stops objects that contain themselves.
        if ($this->nesting >= Phl::MAX_DEPTH) {
            throw new PhlException('nesting is deeper than the limit of ' . Phl::MAX_DEPTH . ' levels');
        }
        $this->nesting++;
        if (self::isObject($value)) {
            $result = $this->object((array) $value, $depth, $inline);
        } elseif (self::isTable($value)) {
            $result = $this->table($value, $depth, $inline);
        } else {
            $result = $this->list($value, $depth, $inline);
        }
        $this->nesting--;
        return $result;
    }

    /** @param list<string> $items */
    private function block(array $items, int $depth, bool $inline, string $open, string $close): string
    {
        if ($inline || $this->indent === '') {
            return $open . implode(', ', $items) . $close;
        }
        $pad = str_repeat($this->indent, $depth + 1);
        return $open . "\n" . $pad . implode(",\n" . $pad, $items) . "\n" . str_repeat($this->indent, $depth) . $close;
    }

    /** @param array<mixed> $value */
    private function object(array $value, int $depth, bool $inline): string
    {
        if ($value === []) {
            return '{}';
        }
        $items = [];
        foreach ($value as $key => $item) {
            $items[] = self::key((string) $key) . ': ' . $this->value($item, $depth + 1, $inline);
        }
        return $this->block($items, $depth, $inline, '{', '}');
    }

    /** @param list<mixed> $value */
    private function list(array $value, int $depth, bool $inline): string
    {
        if ($value === []) {
            return '[]';
        }
        $oneLine = $inline;
        if (!$oneLine) {
            $oneLine = true;
            foreach ($value as $item) {
                if (!self::isScalar($item)) {
                    $oneLine = false;
                    break;
                }
            }
        }
        $items = [];
        foreach ($value as $item) {
            $items[] = $this->value($item, $depth + 1, $oneLine);
        }
        return $this->block($items, $depth, $oneLine, '[', ']');
    }

    /** @param list<mixed> $rows */
    private function table(array $rows, int $depth, bool $inline): string
    {
        $names = [];
        foreach (array_keys((array) $rows[0]) as $name) {
            $names[] = self::key((string) $name);
        }
        $header = '(' . implode(', ', $names) . ')';
        $lines = [];
        foreach ($rows as $row) {
            $cells = [];
            foreach ((array) $row as $cell) {
                $cells[] = $this->value($cell, 0, true);
            }
            $lines[] = implode(', ', $cells) . ';';
        }
        if ($inline || $this->indent === '') {
            return $header . ' { ' . implode(' ', $lines) . ' }';
        }
        $pad = str_repeat($this->indent, $depth + 1);
        return $header . " {\n" . $pad . implode("\n" . $pad, $lines) . "\n" . str_repeat($this->indent, $depth) . '}';
    }
}
