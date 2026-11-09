<?php

declare(strict_types=1);

namespace Phl;

/**
 * Turns PHL and HPL text into tokens. Works on the bytes of the UTF-8 text.
 *
 * @internal
 */
final class Lexer
{
    public const STRING = 0;
    public const INTEGER = 1; // a negative whole number; plain digits are WORD tokens
    public const DECIMAL = 2;
    public const WORD = 3;
    public const PUNCT = 4;
    public const NEWLINE = 5;
    public const END = 6;

    private const WORD_CHARS = 'abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_';
    private const DIGITS = '0123456789';

    private int $pos = 0;
    private int $line = 1;
    private int $lineStart = 0;

    /** @var list<array{0:int,1:string,2:int,3:int,4:int}> kind, text, line, line start, offset */
    private array $tokens = [];

    public function __construct(private readonly string $text)
    {
    }

    /** The column of an offset. Columns count code points, not bytes. */
    public function columnOf(int $lineStart, int $offset): int
    {
        $column = 1;
        for ($i = $lineStart; $i < $offset; $i++) {
            if ((ord($this->text[$i]) & 0xC0) !== 0x80) {
                $column++;
            }
        }
        return $column;
    }

    /** The length of the valid UTF-8 at the start of a text that has an error. */
    public static function validPrefix(string $text): int
    {
        $length = strlen($text);
        $i = 0;
        while ($i < $length) {
            $width = 1;
            $c = ord($text[$i]);
            if ($c >= 0xF0) {
                $width = 4;
            } elseif ($c >= 0xE0) {
                $width = 3;
            } elseif ($c >= 0xC0) {
                $width = 2;
            }
            if (preg_match('//u', substr($text, $i, $width)) !== 1 || $i + $width > $length) {
                return $i;
            }
            $i += $width;
        }
        return $i;
    }

    /** @return list<array{0:int,1:string,2:int,3:int,4:int}> */
    public function run(): array
    {
        $text = $this->text;
        $length = strlen($text);

        if (preg_match('//u', $text) !== 1) {
            $valid = self::validPrefix($text);
            $before = substr($text, 0, $valid);
            $line = substr_count($before, "\n") + 1;
            $newline = strrpos($before, "\n");
            throw new PhlException(
                'the text is not valid UTF-8',
                $line,
                $this->columnOf($newline === false ? 0 : $newline + 1, $valid),
            );
        }

        if (str_starts_with($text, "\u{FEFF}")) {
            $this->pos = 3;
            $this->lineStart = 3;
        }

        while ($this->pos < $length) {
            $c = $text[$this->pos];
            if ($c === "\n") {
                $this->add(self::NEWLINE, "\n", $this->pos);
                $this->pos++;
                $this->line++;
                $this->lineStart = $this->pos;
            } elseif ($c === ' ' || $c === "\t" || $c === "\r") {
                $this->pos++;
            } elseif ($c === '/' && ($text[$this->pos + 1] ?? '') === '/') {
                $end = strpos($text, "\n", $this->pos);
                $this->pos = $end === false ? $length : $end;
            } elseif ($c === '"') {
                $this->readString();
            } elseif ($c === '-' || ($c >= '0' && $c <= '9')) {
                $this->readNumberOrWord();
            } elseif (strpos(self::WORD_CHARS, $c) !== false) {
                $width = strspn($text, self::WORD_CHARS, $this->pos);
                $this->add(self::WORD, substr($text, $this->pos, $width), $this->pos);
                $this->pos += $width;
            } elseif (strpos(':,;[]{}()', $c) !== false) {
                $this->add(self::PUNCT, $c, $this->pos);
                $this->pos++;
            } elseif (ord($c) < 0x20) {
                throw $this->error('unexpected control character ' . ord($c), $this->pos);
            } else {
                throw $this->error("unexpected character '" . $this->characterAt($this->pos) . "'", $this->pos);
            }
        }
        $this->add(self::END, '', $length);
        return $this->tokens;
    }

    private function add(int $kind, string $value, int $offset): void
    {
        $this->tokens[] = [$kind, $value, $this->line, $this->lineStart, $offset];
    }

    private function error(string $message, int $offset): PhlException
    {
        return new PhlException($message, $this->line, $this->columnOf($this->lineStart, $offset));
    }

    private function characterAt(int $offset): string
    {
        $width = 1;
        $length = strlen($this->text);
        while ($offset + $width < $length && (ord($this->text[$offset + $width]) & 0xC0) === 0x80) {
            $width++;
        }
        return substr($this->text, $offset, $width);
    }

    private function readString(): void
    {
        $text = $this->text;
        $length = strlen($text);
        $start = $this->pos;
        $i = $start + 1;
        $value = '';
        while (true) {
            // Everything up to the next character that needs attention.
            $width = strcspn($text, "\"\\\n", $i);
            $value .= substr($text, $i, $width);
            $i += $width;
            if ($i >= $length || $text[$i] === "\n") {
                throw $this->error('unterminated string', $start);
            }
            if ($text[$i] === '"') {
                $this->add(self::STRING, $value, $start);
                $this->pos = $i + 1;
                return;
            }
            switch ($text[$i + 1] ?? '') {
                case 'n':
                    $value .= "\n";
                    break;
                case 't':
                    $value .= "\t";
                    break;
                case '"':
                    $value .= '"';
                    break;
                case '\\':
                    $value .= '\\';
                    break;
                default:
                    $shown = $i + 1 < $length && $text[$i + 1] !== "\n" ? $this->characterAt($i + 1) : '';
                    throw $this->error("invalid escape sequence '\\{$shown}'", $i);
            }
            $i += 2;
        }
    }

    private function readNumberOrWord(): void
    {
        $text = $this->text;
        $length = strlen($text);
        $start = $this->pos;
        $i = $start;
        $negative = $text[$i] === '-';
        if ($negative) {
            $i++;
            $next = $text[$i] ?? '';
            if ($next === '' || $next < '0' || $next > '9') {
                throw $this->error("expected a digit after '-'", $start);
            }
        }
        $i += strspn($text, self::WORD_CHARS, $i);
        $head = substr($text, $start, $i - $start);
        $digits = $negative ? substr($head, 1) : $head;

        if (strspn($digits, self::DIGITS) !== strlen($digits)) {
            if ($negative) {
                throw $this->error("invalid number '" . self::shorten($head) . "'", $start);
            }
            // Keys may start with a digit, for example 2fa_enabled.
            $this->add(self::WORD, $head, $start);
            $this->pos = $i;
            return;
        }

        $after = $text[$i + 1] ?? '';
        if (($text[$i] ?? '') === '.' && $after !== '' && $after >= '0' && $after <= '9') {
            $i++;
            $i += strspn($text, self::DIGITS, $i);
            if ($i < $length && strpos(self::WORD_CHARS, $text[$i]) !== false) {
                throw $this->error("invalid number '" . self::shorten(substr($text, $start, $i + 1 - $start)) . "'", $start);
            }
            $this->add(self::DECIMAL, substr($text, $start, $i - $start), $start);
            $this->pos = $i;
            return;
        }

        // A plain run of digits can be a key or a number; the parser decides.
        $this->add($negative ? self::INTEGER : self::WORD, $head, $start);
        $this->pos = $i;
    }

    public static function shorten(string $text): string
    {
        return strlen($text) > 64 ? substr($text, 0, 64) : $text;
    }
}
