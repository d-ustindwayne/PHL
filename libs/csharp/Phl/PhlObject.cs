using System;
using System.Collections;
using System.Collections.Generic;
using System.Diagnostics.CodeAnalysis;

namespace Phl
{
    /// <summary>
    /// A PHL object: a dictionary whose keys keep the order they were added in.
    /// </summary>
    public sealed class PhlObject : IDictionary<string, object?>, IReadOnlyDictionary<string, object?>
    {
        private readonly List<KeyValuePair<string, object?>> _entries = new List<KeyValuePair<string, object?>>();
        private readonly Dictionary<string, int> _index = new Dictionary<string, int>(StringComparer.Ordinal);

        /// <summary>The value of a key. Setting a new key adds it at the end.</summary>
        public object? this[string key]
        {
            get
            {
                if (key == null) throw new ArgumentNullException(nameof(key));
                if (_index.TryGetValue(key, out int position)) return _entries[position].Value;
                throw new KeyNotFoundException($"no entry named '{key}'");
            }
            set
            {
                if (key == null) throw new ArgumentNullException(nameof(key));
                if (_index.TryGetValue(key, out int position))
                {
                    _entries[position] = new KeyValuePair<string, object?>(key, value);
                }
                else
                {
                    _index[key] = _entries.Count;
                    _entries.Add(new KeyValuePair<string, object?>(key, value));
                }
            }
        }

        /// <summary>The number of entries.</summary>
        public int Count => _entries.Count;

        /// <summary>The keys, in order.</summary>
        public ICollection<string> Keys
        {
            get
            {
                var keys = new List<string>(_entries.Count);
                foreach (var entry in _entries) keys.Add(entry.Key);
                return keys;
            }
        }

        /// <summary>The values, in the order of their keys.</summary>
        public ICollection<object?> Values
        {
            get
            {
                var values = new List<object?>(_entries.Count);
                foreach (var entry in _entries) values.Add(entry.Value);
                return values;
            }
        }

        IEnumerable<string> IReadOnlyDictionary<string, object?>.Keys => Keys;
        IEnumerable<object?> IReadOnlyDictionary<string, object?>.Values => Values;
        bool ICollection<KeyValuePair<string, object?>>.IsReadOnly => false;

        /// <summary>Adds an entry, or throws <see cref="ArgumentException"/> if the key is taken.</summary>
        public void Add(string key, object? value)
        {
            if (key == null) throw new ArgumentNullException(nameof(key));
            if (_index.ContainsKey(key)) throw new ArgumentException($"there is already an entry named '{key}'", nameof(key));
            this[key] = value;
        }

        /// <summary>Whether a key is present.</summary>
        public bool ContainsKey(string key) => _index.ContainsKey(key);

#pragma warning disable CS8767 // netstandard2.0 has no nullability on the interface.
        /// <summary>Gets the value of a key, if it is present.</summary>
        public bool TryGetValue(string key, [MaybeNullWhen(false)] out object? value)
#pragma warning restore CS8767
        {
            if (_index.TryGetValue(key, out int position))
            {
                value = _entries[position].Value;
                return true;
            }
            value = null;
            return false;
        }

        /// <summary>Removes a key. The other keys keep their order.</summary>
        public bool Remove(string key)
        {
            if (!_index.TryGetValue(key, out int position)) return false;
            _entries.RemoveAt(position);
            _index.Remove(key);
            for (int i = position; i < _entries.Count; i++) _index[_entries[i].Key] = i;
            return true;
        }

        /// <summary>Removes all entries.</summary>
        public void Clear()
        {
            _entries.Clear();
            _index.Clear();
        }

        /// <summary>The entries, in order.</summary>
        public IEnumerator<KeyValuePair<string, object?>> GetEnumerator() => _entries.GetEnumerator();

        IEnumerator IEnumerable.GetEnumerator() => _entries.GetEnumerator();

        void ICollection<KeyValuePair<string, object?>>.Add(KeyValuePair<string, object?> item) => Add(item.Key, item.Value);

        bool ICollection<KeyValuePair<string, object?>>.Contains(KeyValuePair<string, object?> item) =>
            TryGetValue(item.Key, out object? value) && Equals(value, item.Value);

        void ICollection<KeyValuePair<string, object?>>.CopyTo(KeyValuePair<string, object?>[] array, int arrayIndex) =>
            _entries.CopyTo(array, arrayIndex);

        bool ICollection<KeyValuePair<string, object?>>.Remove(KeyValuePair<string, object?> item) =>
            ((ICollection<KeyValuePair<string, object?>>)this).Contains(item) && Remove(item.Key);
    }
}

#if NETSTANDARD2_0
namespace System.Diagnostics.CodeAnalysis
{
    [AttributeUsage(AttributeTargets.Parameter)]
    internal sealed class MaybeNullWhenAttribute : Attribute
    {
        public MaybeNullWhenAttribute(bool returnValue) => ReturnValue = returnValue;
        public bool ReturnValue { get; }
    }
}
#endif
