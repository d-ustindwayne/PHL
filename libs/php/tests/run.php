<?php

// Runs the shared conformance cases and checks of the PHP library:
//
//     php tests/run.php

declare(strict_types=1);

use Phl\Phl;
use Phl\PhlException;

spl_autoload_register(static function (string $class): void {
    if (str_starts_with($class, 'Phl\\')) {
        require __DIR__ . '/../src/' . substr($class, 4) . '.php';
    }
});

$passed = 0;
$failed = 0;

function check(string $name, bool $ok): void
{
    global $passed, $failed;
    if ($ok) {
        $passed++;
    } else {
        $failed++;
        echo "FAIL {$name}\n";
    }
}

function fails(callable $action): ?PhlException
{
    try {
        $action();
    } catch (PhlException $e) {
        return $e;
    }
    return null;
}

/** Equal, with the same types and key order all the way down. */
function same(mixed $a, mixed $b): bool
{
    if ($a instanceof stdClass) {
        return $b instanceof stdClass && same((array) $a, (array) $b);
    }
    if (is_array($a)) {
        if (!is_array($b) || array_keys($a) !== array_keys($b)) {
            return false;
        }
        foreach ($a as $key => $value) {
            if (!same($value, $b[$key])) {
                return false;
            }
        }
        return true;
    }
    return $a === $b;
}

$root = __DIR__ . '/../../../conformance';

// PHL documents
foreach (glob("{$root}/phl/valid/*.phl") as $path) {
    $name = basename($path);
    $stem = substr($path, 0, -4);
    $text = file_get_contents($path);
    $json = file_get_contents("{$stem}.json");

    $data = Phl::parse($text);
    check($name, same(json_decode($json, true, 2048), $data));
    foreach ([4, 0] as $indent) {
        check("{$name} (round trip, indent {$indent})", same($data, Phl::parse(Phl::stringify($data, $indent))));
    }

    $objects = Phl::parse($text, objects: true);
    check("{$name} (objects)", same(json_decode($json, false, 2048), $objects));
    foreach ([4, 0] as $indent) {
        check("{$name} (objects, round trip)", same($objects, Phl::parse(Phl::stringify($objects, $indent), objects: true)));
    }

    if (is_file("{$stem}.nocoerce.json")) {
        $raw = json_decode(file_get_contents("{$stem}.nocoerce.json"), true);
        check("{$name} (coerce off)", same($raw, Phl::parse($text, coerce: false)));
    }
}
foreach (glob("{$root}/phl/invalid/*.phl") as $path) {
    $text = file_get_contents($path);
    check(basename($path), fails(static fn () => Phl::parse($text)) !== null);
}

// HPL tables
foreach (glob("{$root}/hpl/valid/*.hpl") as $path) {
    $name = basename($path);
    $rows = Phl::parseTable(file_get_contents($path));
    $json = file_get_contents(substr($path, 0, -4) . '.json');
    check($name, same(json_decode($json, true), $rows));
    check("{$name} (round trip)", same($rows, Phl::parseTable(Phl::stringifyTable($rows))));
    $objects = Phl::parseTable(file_get_contents($path), objects: true);
    check("{$name} (objects)", same(json_decode($json, false), $objects));
    check("{$name} (objects, round trip)", same($objects, Phl::parseTable(Phl::stringifyTable($objects), objects: true)));
}
foreach (glob("{$root}/hpl/invalid/*.hpl") as $path) {
    $text = file_get_contents($path);
    check(basename($path), fails(static fn () => Phl::parseTable($text)) !== null);
}

// Types
$doc = Phl::parse("whole: 42\ndecimal: 34.00\nnegative: -0.5\na: \"True\"\nb: \"NULL\"\n\"true\": 1");
check('whole number', $doc['whole'] === 42);
check('decimal', $doc['decimal'] === 34.0);
check('negative', $doc['negative'] === -0.5);
check('coerced boolean', $doc['a'] === true);
check('coerced nil', array_key_exists('b', $doc) && $doc['b'] === null);
check('keys are not coerced', $doc['true'] === 1);
check('coercion only matches ASCII', Phl::parse("a: \"n\u{130}l\"")['a'] === "n\u{130}l");
foreach (['true', 'false', 'nil'] as $word) {
    check("bare {$word}", fails(static fn () => Phl::parse("a: {$word}")) !== null);
}
$table = Phl::parse('product: (ID, Name) { 101, "a"; 102, "b"; 103, "c"; 104, "d"; }');
check('table index', $table['product'][3]['ID'] === 104);

// Errors
$e = fails(static fn () => Phl::parse("a: 1\nb: oops"));
check('error position', $e && $e->errorLine === 2 && $e->errorColumn === 4);
check('error line in the base class is its own', $e && is_int($e->getLine()));
check('error reason', $e && $e->reason === "unexpected word 'oops'; strings need double quotes");
check('error message', $e && $e->getMessage() === "unexpected word 'oops'; strings need double quotes (line 2, column 4)");
$e = fails(static fn () => Phl::parse("a: \"\u{E9}\u{1F600}\" oops"));
check('columns count characters', $e && $e->errorColumn === 9);
$e = fails(static fn () => Phl::parse("\u{FEFF}a: oops"));
check('a byte order mark is not a column', $e && $e->errorColumn === 4);
$e = fails(static fn () => Phl::parse("a: 1\nb: \"\xFF\""));
check('invalid UTF-8', $e && $e->errorLine === 2 && $e->errorColumn === 5);
check('surrogates are invalid UTF-8', fails(static fn () => Phl::parse("a: \"\xED\xA0\x80\"")) !== null);
check('cut short UTF-8', fails(static fn () => Phl::parse("a: \"\xC3")) !== null);
check('empty text', Phl::parse('') === []);

// Numbers
$doc = Phl::parse("max: 9223372036854775807\nmin: -9223372036854775808\nzeros: 007\nnegzero: -0");
check('largest number', $doc['max'] === PHP_INT_MAX);
check('smallest number', $doc['min'] === PHP_INT_MIN);
check('leading zeros', $doc['zeros'] === 7);
check('negative zero whole number', $doc['negzero'] === 0);
$e = fails(static fn () => Phl::parse('a: 9223372036854775808'));
check('beyond 64 bits', $e && $e->errorColumn === 4);
$e = fails(static fn () => Phl::parse('a: -9223372036854775809'));
check('below 64 bits', $e && $e->errorColumn === 4);
check('long key is fine', array_key_exists('123456789012345678901234567890', Phl::parse('123456789012345678901234567890: 1')));
$e = fails(static fn () => Phl::parse('a: ' . str_repeat('9', 400) . '.0'));
check('too large decimal', $e && $e->errorColumn === 4);
check('too small decimal', Phl::parse('a: 0.' . str_repeat('0', 400) . '1')['a'] === 0.0);
foreach ([5e-324, 1e-310, 2.2250738585072014e-308, 1.7976931348623157e308, 0.1 + 0.2, 1.0 / 3.0, 123456789012345680.0] as $number) {
    check("{$number} reads back the same", Phl::parse(Phl::stringify(['a' => $number]))['a'] === $number);
}

// Writing
$doc = [
    'name' => 'Ann',
    'tags' => ['a', 'b'],
    'server' => ['Host' => 'db', 'Port' => 1341],
    'rows' => [['ID' => 1, 'Name' => 'x'], ['ID' => 2, 'Name' => 'y']],
];
check('layout', Phl::stringify($doc) === <<<'PHL'
    name: "Ann"
    tags: ["a", "b"]
    server: {
        Host: "db",
        Port: 1341
    }
    rows: (ID, Name) {
        1, "x";
        2, "y";
    }

    PHL);
check('indent 0', Phl::stringify($doc, 0) === <<<'PHL'
    name: "Ann"
    tags: ["a", "b"]
    server: {Host: "db", Port: 1341}
    rows: (ID, Name) { 1, "x"; 2, "y"; }

    PHL);
check('quotes keys and escapes strings', Phl::stringify(['a b' => "say \"hi\"\n"]) === "\"a b\": \"say \\\"hi\\\"\\n\"\n");
check('booleans and nil', Phl::stringify(['a' => true, 'b' => false, 'c' => null]) === "a: \"true\"\nb: \"false\"\nc: \"nil\"\n");
check(
    'floats have no exponent',
    Phl::stringify(['small' => 1e-7, 'large' => 1e22, 'whole' => 34.0, 'tenth' => 0.1, 'neg' => -25.99, 'zero' => -0.0])
        === "small: 0.0000001\nlarge: 10000000000000000000000.0\nwhole: 34.0\ntenth: 0.1\nneg: -25.99\nzero: -0.0\n",
);
check('empty document', Phl::stringify([]) === '');
check('HPL table', Phl::stringifyTable(Phl::parseTable('(ID, "full name") { 1, "Ann"; }')) === "(ID, \"full name\") {\n    1, \"Ann\";\n}\n");
check('HPL table on one line', Phl::stringifyTable([['ID' => 1], ['ID' => 2]], 0) === "(ID) { 1; 2; }\n");

$bad = [
    new DateTimeImmutable(),
    NAN,
    INF,
    "\x01",
    "\xFF",
    STDOUT,
];
foreach ($bad as $i => $value) {
    check("refuses value {$i}", fails(static fn () => Phl::stringify(['x' => $value])) !== null);
}
check('a list is not a document', fails(static fn () => Phl::stringify([1, 2])) !== null);
check('an empty table', fails(static fn () => Phl::stringifyTable([])) !== null);
check('different keys are not a table', fails(static fn () => Phl::stringifyTable([['a' => 1], ['b' => 2]])) !== null);
check('key order matters for a table', fails(static fn () => Phl::stringifyTable([['a' => 1, 'b' => 2], ['b' => 3, 'a' => 4]])) !== null);

// What arrays cannot express, and objects: true can.
check('arrays turn {} into []', Phl::stringify(Phl::parse("o: {}\nl: []")) === "o: []\nl: []\n");
check('objects keep {} and []', Phl::stringify(Phl::parse("o: {}\nl: []", objects: true)) === "o: {}\nl: []\n");
check('arrays turn {0: "a"} into a list', Phl::stringify(Phl::parse('o: {0: "a"}')) === "o: [\"a\"]\n");
check('objects keep {0: "a"}', Phl::stringify(Phl::parse('o: {0: "a"}', objects: true)) === "o: {\n    0: \"a\"\n}\n");
check('objects keep a top-level key 0', Phl::stringify(Phl::parse('0: "a"', objects: true)) === "0: \"a\"\n");
check('numeric keys are not lost', Phl::parse('"1": "a", "01": "b"') === [1 => 'a', '01' => 'b']);
check('duplicate numeric keys', fails(static fn () => Phl::parse('"1": 1, 1: 2')) !== null);
check('an empty key', Phl::parse('"": 1') === ['' => 1] && Phl::stringify(['' => 1]) === "\"\": 1\n");
check('a NUL in a key with objects', Phl::parse("\"\x00a\": 1", objects: true) instanceof stdClass);

// Nesting
$open = str_repeat('[', Phl::MAX_DEPTH);
$close = str_repeat(']', Phl::MAX_DEPTH);
check('deepest allowed list is written', Phl::stringify(Phl::parse("a: {$open}{$close}")) !== '');
check('too deep', fails(static fn () => Phl::parse("a: [{$open}{$close}]")) !== null);
check('very deep', fails(static fn () => Phl::parse('a: ' . str_repeat('[', 200000))) !== null);
$cycle = new stdClass();
$cycle->self = $cycle;
check('an object that contains itself', fails(static fn () => Phl::stringify($cycle)) !== null);

// Locales with a decimal comma must change neither reading nor writing.
$locale = setlocale(LC_ALL, 'de_DE.UTF-8', 'de_DE', 'German_Germany', 'deu');
if ($locale !== false) {
    $doc = Phl::parse("a: 1.5\nb: 0.25");
    check('reads decimals in a comma locale', $doc['a'] === 1.5);
    check('writes decimals in a comma locale', Phl::stringify($doc) === "a: 1.5\nb: 0.25\n");
    setlocale(LC_ALL, 'C');
} else {
    echo "note: no comma locale available, skipped the locale check\n";
}

echo "{$passed} passed, {$failed} failed\n";
exit($failed > 0 ? 1 : 0);
