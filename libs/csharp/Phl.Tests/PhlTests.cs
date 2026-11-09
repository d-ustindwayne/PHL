using System;
using System.Collections;
using System.Collections.Generic;
using System.Globalization;
using System.IO;
using System.Linq;
using System.Numerics;
using System.Runtime.CompilerServices;
using System.Text.Json;
using System.Threading;
using Xunit;

namespace Phl.Tests
{
    public class PhlTests
    {
        private static string Conformance([CallerFilePath] string here = "") =>
            Path.GetFullPath(Path.Combine(Path.GetDirectoryName(here)!, "..", "..", "..", "conformance"));

        private static object? FromJson(JsonElement element)
        {
            switch (element.ValueKind)
            {
                case JsonValueKind.Object:
                    var result = new PhlObject();
                    foreach (JsonProperty property in element.EnumerateObject()) result[property.Name] = FromJson(property.Value);
                    return result;
                case JsonValueKind.Array:
                    return element.EnumerateArray().Select(FromJson).ToList();
                case JsonValueKind.String:
                    return element.GetString();
                case JsonValueKind.True:
                    return true;
                case JsonValueKind.False:
                    return false;
                case JsonValueKind.Null:
                    return null;
                default:
                    return element.GetRawText().Contains('.') ? element.GetDouble() : (object)element.GetInt64();
            }
        }

        private static object? ReadJson(string path)
        {
            using JsonDocument document = JsonDocument.Parse(File.ReadAllText(path), new JsonDocumentOptions { MaxDepth = 1024 });
            return FromJson(document.RootElement);
        }

        /// <summary>Equal, with the same types and key order all the way down.</summary>
        private static void AssertSame(object? expected, object? actual)
        {
            if (expected is PhlObject a)
            {
                PhlObject b = Assert.IsType<PhlObject>(actual);
                Assert.Equal(a.Keys, b.Keys);
                foreach (var key in a.Keys) AssertSame(a[key], b[key]);
            }
            else if (expected is IList x)
            {
                IList y = Assert.IsAssignableFrom<IList>(actual);
                Assert.Equal(x.Count, y.Count);
                for (int i = 0; i < x.Count; i++) AssertSame(x[i], y[i]);
            }
            else if (expected == null)
            {
                Assert.Null(actual);
            }
            else
            {
                Assert.Equal(expected.GetType(), actual?.GetType());
                Assert.Equal(expected, actual);
            }
        }

        private static string Stem(string path) => Path.Combine(Path.GetDirectoryName(path)!, Path.GetFileNameWithoutExtension(path));

        public static IEnumerable<object[]> Cases(string folder, string pattern) =>
            Directory.GetFiles(Path.Combine(Conformance(), folder), pattern).OrderBy(p => p).Select(p => new object[] { Path.GetFileName(p) });

        [Theory]
        [MemberData(nameof(Cases), "phl/valid", "*.phl")]
        public void ValidDocuments(string name)
        {
            string path = Path.Combine(Conformance(), "phl", "valid", name);
            string text = File.ReadAllText(path);
            PhlObject data = PhlConvert.Parse(text);
            AssertSame(ReadJson(Stem(path) + ".json"), data);
            string raw = Stem(path) + ".nocoerce.json";
            if (File.Exists(raw)) AssertSame(ReadJson(raw), PhlConvert.Parse(text, coerce: false));
            foreach (int indent in new[] { 4, 0 }) AssertSame(data, PhlConvert.Parse(PhlConvert.Stringify(data, indent)));
        }

        [Theory]
        [MemberData(nameof(Cases), "phl/invalid", "*.phl")]
        public void InvalidDocuments(string name)
        {
            string text = File.ReadAllText(Path.Combine(Conformance(), "phl", "invalid", name));
            Assert.Throws<PhlException>(() => PhlConvert.Parse(text));
        }

        [Theory]
        [MemberData(nameof(Cases), "hpl/valid", "*.hpl")]
        public void ValidTables(string name)
        {
            string path = Path.Combine(Conformance(), "hpl", "valid", name);
            List<PhlObject> rows = PhlConvert.ParseTable(File.ReadAllText(path));
            AssertSame(ReadJson(Stem(path) + ".json"), rows.Cast<object?>().ToList());
            AssertSame(rows.Cast<object?>().ToList(), PhlConvert.ParseTable(PhlConvert.StringifyTable(rows)).Cast<object?>().ToList());
        }

        [Theory]
        [MemberData(nameof(Cases), "hpl/invalid", "*.hpl")]
        public void InvalidTables(string name)
        {
            string text = File.ReadAllText(Path.Combine(Conformance(), "hpl", "invalid", name));
            Assert.Throws<PhlException>(() => PhlConvert.ParseTable(text));
        }

        private static PhlException Failure(string text) => Assert.Throws<PhlException>(() => PhlConvert.Parse(text));

        [Fact]
        public void Types()
        {
            PhlObject doc = PhlConvert.Parse("whole: 42\ndecimal: 34.00\nnegative: -0.5\na: \"True\"\nb: \"NULL\"\n\"true\": 1");
            Assert.Equal(42L, doc["whole"]);
            Assert.Equal(34.0, doc["decimal"]);
            Assert.Equal(-0.5, doc["negative"]);
            Assert.Equal(true, doc["a"]);
            Assert.True(doc.ContainsKey("b"));
            Assert.Null(doc["b"]);
            Assert.Equal(1L, doc["true"]);
        }

        [Fact]
        public void CoercionOnlyMatchesAscii()
        {
            Assert.Equal("n\u0131l", PhlConvert.Parse("a: \"n\u0131l\"")["a"]);
            Assert.Equal("n\u0130l", PhlConvert.Parse("a: \"n\u0130l\"")["a"]);
            Assert.Equal("fal\u017Fe", PhlConvert.Parse("a: \"fal\u017Fe\"")["a"]);
        }

        [Fact]
        public void TableIndex()
        {
            PhlObject doc = PhlConvert.Parse("product: (ID, Name) { 101, \"a\"; 102, \"b\"; 103, \"c\"; 104, \"d\"; }");
            var rows = (List<object?>)doc["product"]!;
            Assert.Equal(104L, ((PhlObject)rows[3]!)["ID"]);
        }

        [Fact]
        public void ErrorPositions()
        {
            PhlException error = Failure("a: 1\nb: oops");
            Assert.Equal(2, error.Line);
            Assert.Equal(4, error.Column);
            Assert.Equal("unexpected word 'oops'; strings need double quotes", error.Reason);
            Assert.Equal("unexpected word 'oops'; strings need double quotes (line 2, column 4)", error.Message);

            Assert.Equal(10, Failure("a: \"\U0001F600\U0001F600\u00E9\" oops").Column);
            Assert.Equal(4, Failure("\uFEFFa: oops").Column);
            Assert.Equal(8, Failure("a: \"x\" \"y\"").Column);
        }

        [Fact]
        public void Numbers()
        {
            PhlObject doc = PhlConvert.Parse("max: 9223372036854775807\nmin: -9223372036854775808\nzeros: 007\nnegzero: -0");
            Assert.Equal(long.MaxValue, doc["max"]);
            Assert.Equal(long.MinValue, doc["min"]);
            Assert.Equal(7L, doc["zeros"]);
            Assert.Equal(0L, doc["negzero"]);

            Assert.Equal(4, Failure("a: 9223372036854775808").Column);
            Assert.Equal(4, Failure("a: -9223372036854775809").Column);
            Assert.True(PhlConvert.Parse("123456789012345678901234567890: 1").ContainsKey("123456789012345678901234567890"));

            Assert.Equal(4, Failure("a: " + new string('9', 400) + ".0").Column);
            Assert.Equal(0.0, PhlConvert.Parse("a: 0." + new string('0', 400) + "1")["a"]);

            foreach (double number in new[] { 5e-324, 1e-310, 2.2250738585072014e-308, 1.7976931348623157e308, 0.1 + 0.2, 1.0 / 3.0 })
            {
                Assert.Equal(number, PhlConvert.Parse(PhlConvert.Stringify(new PhlObject { ["a"] = number }))["a"]);
            }
        }

        [Fact]
        public void Layout()
        {
            var doc = new PhlObject
            {
                ["name"] = "Ann",
                ["tags"] = new[] { "a", "b" },
                ["server"] = new PhlObject { ["Host"] = "db", ["Port"] = 1341 },
                ["rows"] = new List<object?> { new PhlObject { ["ID"] = 1, ["Name"] = "x" }, new PhlObject { ["ID"] = 2, ["Name"] = "y" } },
            };
            Assert.Equal(
                "name: \"Ann\"\n" +
                "tags: [\"a\", \"b\"]\n" +
                "server: {\n" +
                "    Host: \"db\",\n" +
                "    Port: 1341\n" +
                "}\n" +
                "rows: (ID, Name) {\n" +
                "    1, \"x\";\n" +
                "    2, \"y\";\n" +
                "}\n",
                PhlConvert.Stringify(doc));
            Assert.Equal(
                "name: \"Ann\"\n" +
                "tags: [\"a\", \"b\"]\n" +
                "server: {Host: \"db\", Port: 1341}\n" +
                "rows: (ID, Name) { 1, \"x\"; 2, \"y\"; }\n",
                PhlConvert.Stringify(doc, 0));
        }

        [Fact]
        public void Writing()
        {
            Assert.Equal("\"a b\": \"say \\\"hi\\\"\\n\"\n", PhlConvert.Stringify(new PhlObject { ["a b"] = "say \"hi\"\n" }));
            Assert.Equal("a: \"true\"\nb: \"false\"\nc: \"nil\"\n", PhlConvert.Stringify(new PhlObject { ["a"] = true, ["b"] = false, ["c"] = null }));
            Assert.Equal(
                "small: 0.0000001\nlarge: 10000000000000000000000.0\nwhole: 34.0\ntenth: 0.1\nneg: -25.99\nzero: -0.0\n",
                PhlConvert.Stringify(new PhlObject { ["small"] = 1e-7, ["large"] = 1e22, ["whole"] = 34.0, ["tenth"] = 0.1, ["neg"] = -25.99, ["zero"] = -0.0 }));
            Assert.Equal("o: {}\nl: []\n", PhlConvert.Stringify(PhlConvert.Parse("o: {}\nl: []")));
            Assert.Equal("", PhlConvert.Stringify(new PhlObject()));
            Assert.Equal("(ID, \"full name\") {\n    1, \"Ann\";\n}\n", PhlConvert.StringifyTable(PhlConvert.ParseTable("(ID, \"full name\") { 1, \"Ann\"; }")));
            Assert.Equal("(ID) { 1; 2; }\n", PhlConvert.StringifyTable(new[] { new PhlObject { ["ID"] = 1 }, new PhlObject { ["ID"] = 2 } }, 0));
        }

        [Fact]
        public void WritingOtherTypes()
        {
            Assert.Equal("a: 1\nb: 2\n", PhlConvert.Stringify(new Dictionary<string, int> { ["a"] = 1, ["b"] = 2 }));
            Assert.Equal("a: \"x\"\n", PhlConvert.Stringify(new Dictionary<string, object?> { ["a"] = "x" }));
            Assert.Equal("a: 0.1\n", PhlConvert.Stringify(new PhlObject { ["a"] = 0.1f }));
            Assert.Equal("a: 1.25\n", PhlConvert.Stringify(new PhlObject { ["a"] = 1.25m }));
            Assert.Equal("a: 5.0\n", PhlConvert.Stringify(new PhlObject { ["a"] = 5m }));
            Assert.Equal("a: 7\n", PhlConvert.Stringify(new PhlObject { ["a"] = new BigInteger(7) }));
            Assert.Equal("a: 18\n", PhlConvert.Stringify(new PhlObject { ["a"] = (ulong)18 }));
            Assert.Equal("a: [1, 2]\n", PhlConvert.Stringify(new PhlObject { ["a"] = new[] { 1, 2 } }));
            Assert.Equal("a: [1, 2]\n", PhlConvert.Stringify(new PhlObject { ["a"] = Enumerable.Range(1, 2) }));
            Assert.Equal("(b) {\n    1;\n}\n", PhlConvert.StringifyTable(new List<Dictionary<string, int>> { new Dictionary<string, int> { ["b"] = 1 } }));
        }

        [Fact]
        public void RefusesWhatCannotBeWritten()
        {
            foreach (object bad in new object[]
            {
                new object(),
                double.NaN,
                double.PositiveInfinity,
                float.NaN,
                "\u0001",
                ulong.MaxValue,
                BigInteger.One << 64,
                new Dictionary<int, string> { [1] = "a" },
            })
            {
                Assert.Throws<PhlException>(() => PhlConvert.Stringify(new PhlObject { ["x"] = bad }));
            }
            Assert.Throws<PhlException>(() => PhlConvert.Stringify(null!));
            Assert.Throws<PhlException>(() => PhlConvert.Stringify(new[] { 1 }));
            Assert.Throws<PhlException>(() => PhlConvert.Stringify("text"));
            Assert.Throws<PhlException>(() => PhlConvert.StringifyTable(new List<PhlObject>()));
            Assert.Throws<PhlException>(() => PhlConvert.StringifyTable("text"));
            Assert.Throws<PhlException>(() => PhlConvert.StringifyTable(new[] { new PhlObject { ["a"] = 1 }, new PhlObject { ["b"] = 2 } }));
        }

        [Fact]
        public void NestingLimit()
        {
            string open = new string('[', PhlConvert.MaxDepth);
            string close = new string(']', PhlConvert.MaxDepth);
            Assert.NotEmpty(PhlConvert.Stringify(PhlConvert.Parse("a: " + open + close)));
            Failure("a: [" + open + close + "]");
            Failure("a: " + new string('[', 200000));

            // A value that contains itself fails with an error, not a stack overflow,
            // which .NET could not recover from.
            var cycle = new List<object?>();
            cycle.Add(cycle);
            Assert.Throws<PhlException>(() => PhlConvert.Stringify(new PhlObject { ["a"] = cycle }));
        }

        [Fact]
        public void Readers()
        {
            Assert.Equal(1L, PhlConvert.Parse(new StringReader("a: 1"))["a"]);
            Assert.Equal(1L, PhlConvert.ParseTable(new StringReader("(a) { 1; }"))[0]["a"]);
        }

        [Fact]
        public void CultureDoesNotMatter()
        {
            CultureInfo saved = CultureInfo.CurrentCulture;
            try
            {
                CultureInfo.CurrentCulture = new CultureInfo("de-DE");
                PhlObject doc = PhlConvert.Parse("a: 1.5\nb: \"TRUE\"\nc: -3");
                Assert.Equal(1.5, doc["a"]);
                Assert.Equal(true, doc["b"]);
                Assert.Equal("a: 1.5\nb: \"true\"\nc: -3\n", PhlConvert.Stringify(doc));
                CultureInfo.CurrentCulture = new CultureInfo("tr-TR");
                Assert.Equal(true, PhlConvert.Parse("a: \"TRUE\"")["a"]);
                Assert.Null(PhlConvert.Parse("a: \"NIL\"")["a"]);
            }
            finally
            {
                CultureInfo.CurrentCulture = saved;
            }
        }

        [Fact]
        public void ObjectsKeepTheirOrder()
        {
            PhlObject doc = PhlConvert.Parse("z: 1\na: 2\nm: 3");
            Assert.Equal(new[] { "z", "a", "m" }, doc.Keys);
            doc.Remove("a");
            doc["b"] = 4;
            Assert.Equal(new[] { "z", "m", "b" }, doc.Keys);
            Assert.Equal(2, doc.Keys.ToList().IndexOf("b"));
            Assert.Throws<KeyNotFoundException>(() => doc["nothing"]);
            Assert.Throws<ArgumentException>(() => doc.Add("z", 1));
            Assert.Equal(3, doc.Count);
        }
    }
}
