<?php

declare(strict_types=1);

namespace Phl;

/**
 * Builds PHP values from PHL and HPL tokens.
 *
 * @internal
 */
final class Parser
{
    private readonly Lexer $lexer;

    /** @var list<array{0:int,1:string,2:int,3:int,4:int}> */
    private readonly array $tokens;
    private readonly int $last;
    private int $pos = 0;
    private int $depth = 0;

    public function __construct(string $text, private readonly bool $coerce, private readonly bool $objects)
    {
        $this->lexer = new Lexer($text);
        $this->tokens = $this->lexer->run();
        $this->last = count($this->tokens) - 1;
    }

    // Token helpers

    /** @return array{0:int,1:string,2:int,3:int,4:int} */
    private function peek(int $offset = 0): array
    {
        return $this->tokens[min($this->pos + $offset, $this->last)];
    }

    /** @return array{0:int,1:string,2:int,3:int,4:int} */
    private function next(): array
    {
        $token = $this->tokens[$this->pos];
        if ($token[0] !== Lexer::END) {
            $this->pos++;
        }
        return $token;
    }

    private function skipNewlines(): void
    {
        while ($this->tokens[min($this->pos, $this->last)][0] === Lexer::NEWLINE) {
            $this->pos++;
        }
    }

    /** @param array{0:int,1:string,2:int,3:int,4:int} $token */
    private static function isPunct(array $token, string $c): bool
    {
        return $token[0] === Lexer::PUNCT && $token[1] === $c;
    }

    /** @param array{0:int,1:string,2:int,3:int,4:int} $token */
    private static function isKeyLike(array $token): bool
    {
        return $token[0] === Lexer::WORD || $token[0] === Lexer::STRING;
    }

    /** @param array{0:int,1:string,2:int,3:int,4:int} $token */
    private static function describe(array $token): string
    {
        return match ($token[0]) {
            Lexer::END => 'end of input',
            Lexer::NEWLINE => 'end of line',
            Lexer::STRING => 'string',
            default => "'" . Lexer::shorten($token[1]) . "'",
        };
    }

    /** @param array{0:int,1:string,2:int,3:int,4:int} $token */
    private function error(string $message, array $token): PhlException
    {
        return new PhlException($message, $token[2], $this->lexer->columnOf($token[3], $token[4]));
    }

    private function isKeyAhead(): bool
    {
        if (!self::isKeyLike($this->peek())) {
            return false;
        }
        $offset = 1;
        while ($this->peek($offset)[0] === Lexer::NEWLINE) {
            $offset++;
        }
        return self::isPunct($this->peek($offset), ':');
    }

    /** @param array{0:int,1:string,2:int,3:int,4:int} $open */
    private function enter(array $open): void
    {
        if ($this->depth >= Phl::MAX_DEPTH) {
            throw $this->error('nesting is deeper than the limit of ' . Phl::MAX_DEPTH . ' levels', $open);
        }
        $this->depth++;
    }

    /**
     * @param array<mixed> $entries
     *
     * @return array<mixed>|\stdClass
     */
    private function finishObject(array $entries): array|\stdClass
    {
        return $this->objects ? (object) $entries : $entries;
    }

    // Documents

    /** @return array<mixed>|\stdClass */
    public function document(): array|\stdClass
    {
        $result = [];
        $this->skipNewlines();
        while ($this->peek()[0] !== Lexer::END) {
            $this->entry($result);
            $token = $this->peek();
            if (self::isPunct($token, ',')) {
                $this->next();
                $this->skipNewlines();
            } elseif ($token[0] === Lexer::NEWLINE) {
                $this->skipNewlines();
            } elseif ($token[0] !== Lexer::END) {
                throw $this->error("expected a new line or ',' between entries, found " . self::describe($token), $token);
            }
        }
        return $this->finishObject($result);
    }

    /** @return list<mixed> */
    public function tableDocument(): array
    {
        $this->skipNewlines();
        $first = $this->peek();
        if (!self::isPunct($first, '(')) {
            throw $this->error("expected '(' to start the column list, found " . self::describe($first), $first);
        }
        $rows = $this->table();
        $this->skipNewlines();
        $token = $this->peek();
        if ($token[0] !== Lexer::END) {
            throw $this->error('unexpected ' . self::describe($token) . ' after the table', $token);
        }
        return $rows;
    }

    // Entries and values

    /** @param array<mixed> $target */
    private function entry(array &$target): void
    {
        $keyToken = $this->next();
        if (!self::isKeyLike($keyToken)) {
            throw $this->error('expected a key, found ' . self::describe($keyToken), $keyToken);
        }
        $key = $keyToken[1];
        $this->skipNewlines();
        $colon = $this->next();
        if (!self::isPunct($colon, ':')) {
            throw $this->error("expected ':' after key '" . Lexer::shorten($key) . "', found " . self::describe($colon), $colon);
        }
        $this->skipNewlines();
        if (array_key_exists($key, $target)) {
            throw $this->error("duplicate key '" . Lexer::shorten($key) . "'", $keyToken);
        }
        $target[$key] = $this->value();
    }

    /** @param array{0:int,1:string,2:int,3:int,4:int} $token */
    private function stringValue(array $token): mixed
    {
        $text = $token[1];
        $length = strlen($text);
        if ($this->coerce && $length >= 3 && $length <= 5) {
            // strtr, as strtolower depends on the locale before PHP 8.2.
            switch (strtr($text, 'ABCDEFGHIJKLMNOPQRSTUVWXYZ', 'abcdefghijklmnopqrstuvwxyz')) {
                case 'true':
                    return true;
                case 'false':
                    return false;
                case 'nil':
                case 'null':
                case 'none':
                    return null;
            }
        }
        return $text;
    }

    /** @param array{0:int,1:string,2:int,3:int,4:int} $token */
    private function integerValue(array $token): int
    {
        $text = $token[1];
        $negative = $text[0] === '-';
        $digits = ltrim($negative ? substr($text, 1) : $text, '0');
        $limit = $negative ? substr((string) PHP_INT_MIN, 1) : (string) PHP_INT_MAX;
        if (strlen($digits) > strlen($limit) || (strlen($digits) === strlen($limit) && strcmp($digits, $limit) > 0)) {
            $shown = strlen($text) > 40 ? substr($text, 0, 40) . '...' : $text;
            throw $this->error("the number {$shown} does not fit in " . (PHP_INT_SIZE * 8) . ' bits', $token);
        }
        return (int) $text;
    }

    /** @param array{0:int,1:string,2:int,3:int,4:int} $token */
    private function decimalValue(array $token): float
    {
        $number = (float) $token[1];
        if (is_infinite($number)) {
            throw $this->error('this number is too large for a 64-bit decimal', $token);
        }
        return $number;
    }

    private function value(): mixed
    {
        $token = $this->peek();
        switch ($token[0]) {
            case Lexer::STRING:
                $this->pos++;
                return $this->stringValue($token);
            case Lexer::INTEGER:
                $this->pos++;
                return $this->integerValue($token);
            case Lexer::DECIMAL:
                $this->pos++;
                return $this->decimalValue($token);
            case Lexer::WORD:
                $this->pos++;
                if (strspn($token[1], '0123456789') === strlen($token[1])) {
                    return $this->integerValue($token);
                }
                throw $this->error("unexpected word '" . Lexer::shorten($token[1]) . "'; strings need double quotes", $token);
            case Lexer::PUNCT:
                if ($token[1] === '{') {
                    return $this->object();
                }
                if ($token[1] === '[') {
                    return $this->list();
                }
                if ($token[1] === '(') {
                    return $this->table();
                }
                break;
        }
        throw $this->error('expected a value, found ' . self::describe($token), $token);
    }

    /** @return array<mixed>|\stdClass */
    private function object(): array|\stdClass
    {
        $this->enter($this->next());
        $result = [];
        while (true) {
            $this->skipNewlines();
            if (self::isPunct($this->peek(), '}')) {
                $this->next();
                break;
            }
            if (!$this->isKeyAhead()) {
                throw $this->error("expected 'key: value' inside an object", $this->peek());
            }
            $this->entry($result);
            if (!$this->itemSeparator('object', '}')) {
                break;
            }
        }
        $this->depth--;
        return $this->finishObject($result);
    }

    /** @return list<mixed> */
    private function list(): array
    {
        $this->enter($this->next());
        $result = [];
        while (true) {
            $this->skipNewlines();
            if (self::isPunct($this->peek(), ']')) {
                $this->next();
                break;
            }
            if ($this->isKeyAhead()) {
                throw $this->error("a list cannot contain 'key: value' entries; use { } for an object", $this->peek());
            }
            $result[] = $this->value();
            if (!$this->itemSeparator('list', ']')) {
                break;
            }
        }
        $this->depth--;
        return $result;
    }

    /** Reads ',' (gives true) or the closing bracket (gives false). */
    private function itemSeparator(string $kind, string $closer): bool
    {
        $this->skipNewlines();
        $token = $this->next();
        if (self::isPunct($token, ',')) {
            return true;
        }
        if (self::isPunct($token, $closer)) {
            return false;
        }
        throw $this->error("expected ',' or '{$closer}' in {$kind}, found " . self::describe($token), $token);
    }

    // Tables

    /**
     * (col, col) { value, value; value, value; }
     *
     * @return list<mixed>
     */
    private function table(): array
    {
        $this->enter($this->next());
        $columns = $this->columns();
        $count = count($columns);
        $this->skipNewlines();
        $open = $this->next();
        if (!self::isPunct($open, '{')) {
            throw $this->error("expected '{' to start the table rows, found " . self::describe($open), $open);
        }
        $rows = [];
        while (true) {
            $this->skipNewlines();
            if (self::isPunct($this->peek(), '}')) {
                $this->next();
                break;
            }
            $rowToken = $this->peek();
            $cells = [];
            while (true) {
                $this->skipNewlines();
                $cells[] = $this->value();
                $this->skipNewlines();
                $token = $this->next();
                if (self::isPunct($token, ';')) {
                    break;
                }
                if (!self::isPunct($token, ',')) {
                    throw $this->error("expected ',' or ';' in table row, found " . self::describe($token), $token);
                }
            }
            if (count($cells) !== $count) {
                throw $this->error('row has ' . count($cells) . " values but the table has {$count} columns", $rowToken);
            }
            $row = [];
            foreach ($columns as $i => $column) {
                $row[$column] = $cells[$i];
            }
            $rows[] = $this->finishObject($row);
        }
        $this->depth--;
        return $rows;
    }

    /** @return list<string> */
    private function columns(): array
    {
        $columns = [];
        $seen = [];
        while (true) {
            $this->skipNewlines();
            if ($columns !== [] && self::isPunct($this->peek(), ')')) {
                $this->next();
                return $columns;
            }
            $token = $this->next();
            if (!self::isKeyLike($token)) {
                throw $this->error('expected a column name, found ' . self::describe($token), $token);
            }
            if (isset($seen[$token[1]])) {
                throw $this->error("duplicate column '" . Lexer::shorten($token[1]) . "'", $token);
            }
            $seen[$token[1]] = true;
            $columns[] = $token[1];
            $this->skipNewlines();
            $token = $this->next();
            if (self::isPunct($token, ')')) {
                return $columns;
            }
            if (!self::isPunct($token, ',')) {
                throw $this->error("expected ',' or ')' in column list, found " . self::describe($token), $token);
            }
        }
    }
}
