/* The C version of the PHL parser and writer, built on the C library in
   libs/c. It behaves like _parser.py and _writer.py, which are used when this
   module was not compiled. tests/test_parity.py compares the two. */

#define PY_SSIZE_T_CLEAN
#include <Python.h>

#include "phl.h"
#include "internal.h"

static PyObject *phl_error_type = NULL;
static PyObject *mapping_type = NULL;

/* Raises PHLError with an optional position. */
static PyObject *raise_error(const char *message, size_t line, size_t column) {
    PyObject *text = PyUnicode_DecodeUTF8(message, (Py_ssize_t)strlen(message), "replace");
    PyObject *error;
    if (!text) return NULL;
    if (line > 0) {
        error = PyObject_CallFunction(phl_error_type, "Onn", text, (Py_ssize_t)line, (Py_ssize_t)column);
    } else {
        error = PyObject_CallFunction(phl_error_type, "O", text);
    }
    Py_DECREF(text);
    if (error) {
        PyErr_SetObject(phl_error_type, error);
        Py_DECREF(error);
    }
    return NULL;
}

static PyObject *decode(const char *text, size_t length) {
    return PyUnicode_DecodeUTF8(text, (Py_ssize_t)length, "surrogatepass");
}

/* Reading */

static PyObject *to_python(const phl_value *value) {
    size_t i, count;
    PyObject *result;

    switch (phl_type_of(value)) {
        case PHL_NIL:
            Py_RETURN_NONE;
        case PHL_BOOL:
            return PyBool_FromLong(phl_as_bool(value));
        case PHL_INT:
            return PyLong_FromLongLong(phl_as_int(value));
        case PHL_FLOAT:
            return PyFloat_FromDouble(phl_as_float(value));
        case PHL_STRING: {
            size_t length;
            const char *text = phl_as_string(value, &length);
            return decode(text, length);
        }
        case PHL_BIGINT:
            result = PyLong_FromString(phl_as_string(value, NULL), NULL, 10);
            if (!result && PyErr_ExceptionMatches(PyExc_ValueError)) {
                /* More digits than Python allows; see sys.set_int_max_str_digits. */
                PyErr_Clear();
                raise_error("this number has too many digits", value->as.string.line, value->as.string.column);
            }
            return result;
        case PHL_LIST:
            count = phl_length(value);
            result = PyList_New((Py_ssize_t)count);
            if (!result) return NULL;
            for (i = 0; i < count; i++) {
                PyObject *item = to_python(phl_at(value, i));
                if (!item) {
                    Py_DECREF(result);
                    return NULL;
                }
                PyList_SetItem(result, (Py_ssize_t)i, item);
            }
            return result;
        case PHL_OBJECT:
            count = phl_length(value);
            result = PyDict_New();
            if (!result) return NULL;
            for (i = 0; i < count; i++) {
                size_t length;
                const char *text = phl_key_at(value, i, &length);
                PyObject *key = decode(text, length);
                PyObject *item;
                if (!key) {
                    Py_DECREF(result);
                    return NULL;
                }
                /* The rows of a table then share their key strings. */
                PyUnicode_InternInPlace(&key);
                item = to_python(phl_value_at(value, i));
                if (!item || PyDict_SetItem(result, key, item) != 0) {
                    Py_XDECREF(item);
                    Py_DECREF(key);
                    Py_DECREF(result);
                    return NULL;
                }
                Py_DECREF(item);
                Py_DECREF(key);
            }
            return result;
    }
    PyErr_SetString(PyExc_SystemError, "unknown PHL value type");
    return NULL;
}

static PyObject *read_text(PyObject *args, PyObject *kwargs, int table) {
    static char *keywords[] = {"text", "coerce", NULL};
    PyObject *text;
    int coerce = 1;
    PyObject *encoded;
    char *data;
    Py_ssize_t length;
    phl_error error;
    phl_value *value;
    PyObject *result;
    unsigned flags = PHL_KEEP_BIG_INTEGERS | PHL_ASSUME_VALID_UTF8;

    if (!PyArg_ParseTupleAndKeywords(args, kwargs, "U|$p", keywords, &text, &coerce)) return NULL;
    if (!coerce) flags |= PHL_NO_COERCE;

    /* A str can hold lone surrogates, which plain UTF-8 cannot. */
    encoded = PyUnicode_AsEncodedString(text, "utf-8", "surrogatepass");
    if (!encoded) return NULL;
    if (PyBytes_AsStringAndSize(encoded, &data, &length) != 0) {
        Py_DECREF(encoded);
        return NULL;
    }

    value = table ? phl_parse_table(data, (size_t)length, flags, &error) : phl_parse(data, (size_t)length, flags, &error);
    if (!value) {
        raise_error(error.message, error.line, error.column);
        Py_DECREF(encoded);
        return NULL;
    }
    Py_DECREF(encoded);
    result = to_python(value);
    phl_free(value);
    return result;
}

static PyObject *speedups_loads(PyObject *self, PyObject *args, PyObject *kwargs) {
    (void)self;
    return read_text(args, kwargs, 0);
}

static PyObject *speedups_loads_table(PyObject *self, PyObject *args, PyObject *kwargs) {
    (void)self;
    return read_text(args, kwargs, 1);
}

/* Writing */

static PyObject *type_name(PyObject *object) {
    return PyObject_GetAttrString((PyObject *)Py_TYPE(object), "__name__");
}

static void raise_about_type(const char *format, PyObject *object) {
    PyObject *name = type_name(object);
    PyObject *message;
    if (!name) return;
    message = PyUnicode_FromFormat(format, name);
    Py_DECREF(name);
    if (message) {
        PyErr_SetObject(phl_error_type, message);
        Py_DECREF(message);
    }
}

static int is_mapping(PyObject *object) {
    return PyDict_Check(object) ? 1 : PyObject_IsInstance(object, mapping_type);
}

/* The UTF-8 of a str. The bytes object keeps the text alive. */
static const char *utf8_of(PyObject *text, PyObject **holder, Py_ssize_t *length) {
    char *data;
    *holder = PyUnicode_AsEncodedString(text, "utf-8", "surrogatepass");
    if (!*holder) return NULL;
    if (PyBytes_AsStringAndSize(*holder, &data, length) != 0) {
        Py_CLEAR(*holder);
        return NULL;
    }
    return data;
}

static phl_value *from_python(PyObject *object, int depth);

static phl_value *from_mapping(PyObject *object, int depth) {
    PyObject *items = PyMapping_Items(object);
    phl_value *result;
    Py_ssize_t i, count;

    if (!items) return NULL;
    count = PyList_Size(items);
    result = phl_new_object();
    if (!result) {
        Py_DECREF(items);
        PyErr_NoMemory();
        return NULL;
    }
    for (i = 0; i < count; i++) {
        PyObject *pair = PyList_GetItem(items, i);
        PyObject *key_object = pair ? PyTuple_GetItem(pair, 0) : NULL;
        PyObject *item_object = pair ? PyTuple_GetItem(pair, 1) : NULL;
        PyObject *holder;
        const char *key_text;
        Py_ssize_t key_length;
        phl_key *key;
        phl_value *item;

        if (!key_object || !item_object) goto fail;
        if (!PyUnicode_Check(key_object)) {
            raise_about_type("keys must be strings, not %U", key_object);
            goto fail;
        }
        key_text = utf8_of(key_object, &holder, &key_length);
        if (!key_text) goto fail;
        key = phl__key_new(key_text, (size_t)key_length);
        Py_DECREF(holder);
        if (!key) {
            PyErr_NoMemory();
            goto fail;
        }
        item = from_python(item_object, depth);
        if (!item) {
            phl__key_release(key);
            goto fail;
        }
        /* The keys of a mapping are unique already. */
        if (phl__object_append(result, key, item) != 0) {
            phl_free(item);
            phl__key_release(key);
            PyErr_NoMemory();
            goto fail;
        }
        phl__key_release(key);
    }
    Py_DECREF(items);
    return result;

fail:
    Py_DECREF(items);
    phl_free(result);
    return NULL;
}

static phl_value *from_sequence(PyObject *object, int depth) {
    PyObject *items = PySequence_List(object);
    phl_value *result;
    Py_ssize_t i, count;

    if (!items) return NULL;
    count = PyList_Size(items);
    result = phl_new_list();
    if (!result) {
        Py_DECREF(items);
        PyErr_NoMemory();
        return NULL;
    }
    for (i = 0; i < count; i++) {
        PyObject *item_object = PyList_GetItem(items, i);
        phl_value *item = item_object ? from_python(item_object, depth) : NULL;
        if (!item) goto fail;
        if (phl__list_append(result, item) != 0) {
            phl_free(item);
            PyErr_NoMemory();
            goto fail;
        }
    }
    Py_DECREF(items);
    return result;

fail:
    Py_DECREF(items);
    phl_free(result);
    return NULL;
}

/* Sets a Python exception and returns NULL on failure. */
static phl_value *checked(phl_value *value) {
    if (!value) PyErr_NoMemory();
    return value;
}

static phl_value *from_python(PyObject *object, int depth) {
    int mapping;

    if (object == Py_None) return checked(phl_new_nil());
    if (PyBool_Check(object)) return checked(phl_new_bool(object == Py_True));
    if (PyLong_Check(object)) {
        PyObject *digits;
        PyObject *holder;
        const char *text;
        Py_ssize_t length;
        phl_value *value;
        if (PyLong_CheckExact(object)) {
            int overflow = 0;
            long long number = PyLong_AsLongLongAndOverflow(object, &overflow);
            if (!overflow) {
                if (number == -1 && PyErr_Occurred()) return NULL;
                return checked(phl_new_int(number));
            }
        }
        digits = PyObject_Str(object);
        if (!digits) return NULL;
        text = utf8_of(digits, &holder, &length);
        Py_DECREF(digits);
        if (!text) return NULL;
        value = checked(phl__new_bigint(text, (size_t)length, 0, 0));
        Py_DECREF(holder);
        return value;
    }
    if (PyFloat_Check(object)) return checked(phl_new_float(PyFloat_AsDouble(object)));
    if (PyUnicode_Check(object)) {
        PyObject *holder;
        Py_ssize_t length;
        const char *text = utf8_of(object, &holder, &length);
        phl_value *value;
        if (!text) return NULL;
        value = checked(phl_new_string_n(text, (size_t)length));
        Py_DECREF(holder);
        return value;
    }

    mapping = is_mapping(object);
    if (mapping < 0) return NULL;
    if (mapping || PyList_Check(object) || PyTuple_Check(object)) {
        /* This stops values that contain themselves. The writer applies the exact
           limit, and it does not count table rows, which are counted here. */
        if (depth >= 2 * PHL_MAX_DEPTH + 2) {
            PyErr_Format(phl_error_type, "nesting is deeper than the limit of %d levels", PHL_MAX_DEPTH);
            return NULL;
        }
        return mapping ? from_mapping(object, depth + 1) : from_sequence(object, depth + 1);
    }
    raise_about_type("cannot write a value of type %U", object);
    return NULL;
}

static int parse_indent(PyObject *indent_object, int *indent) {
    long number;
    if (indent_object == NULL || indent_object == Py_None) {
        *indent = indent_object == NULL ? 4 : 0;
        return 0;
    }
    number = PyLong_AsLong(indent_object);
    if (number == -1 && PyErr_Occurred()) return -1;
    if (number > 1024) number = 1024;
    if (number < 0) number = 0;
    *indent = (int)number;
    return 0;
}

static PyObject *finish_writing(phl_value *value, int indent, int table) {
    phl_error error;
    size_t length;
    unsigned flags = PHL_ASSUME_VALID_UTF8;
    char *text = table ? phl_write_table(value, indent, flags, &length, &error)
                       : phl_write(value, indent, flags, &length, &error);
    PyObject *result;
    phl_free(value);
    if (!text) return raise_error(error.message, 0, 0);
    result = decode(text, length);
    phl_free_text(text);
    return result;
}

static PyObject *speedups_dumps(PyObject *self, PyObject *args, PyObject *kwargs) {
    static char *keywords[] = {"obj", "indent", NULL};
    PyObject *object;
    PyObject *indent_object = NULL;
    int indent, mapping;
    phl_value *value;

    (void)self;
    if (!PyArg_ParseTupleAndKeywords(args, kwargs, "O|$O", keywords, &object, &indent_object)) return NULL;
    if (parse_indent(indent_object, &indent) != 0) return NULL;
    mapping = is_mapping(object);
    if (mapping < 0) return NULL;
    if (!mapping) return raise_error("a PHL document must be a dict of top-level entries", 0, 0);
    value = from_python(object, -1);
    if (!value) return NULL;
    return finish_writing(value, indent, 0);
}

static PyObject *speedups_dumps_table(PyObject *self, PyObject *args, PyObject *kwargs) {
    static char *keywords[] = {"rows", "indent", NULL};
    PyObject *rows;
    PyObject *indent_object = NULL;
    PyObject *list;
    int indent;
    phl_value *value;

    (void)self;
    if (!PyArg_ParseTupleAndKeywords(args, kwargs, "O|$O", keywords, &rows, &indent_object)) return NULL;
    if (parse_indent(indent_object, &indent) != 0) return NULL;
    list = PySequence_List(rows);
    if (!list) return NULL;
    value = from_sequence(list, 0);
    Py_DECREF(list);
    if (!value) return NULL;
    return finish_writing(value, indent, 1);
}

static PyMethodDef methods[] = {
    {"loads", (PyCFunction)(void (*)(void))speedups_loads, METH_VARARGS | METH_KEYWORDS,
     "Parse a PHL document into a dict."},
    {"loads_table", (PyCFunction)(void (*)(void))speedups_loads_table, METH_VARARGS | METH_KEYWORDS,
     "Parse an HPL table document into a list of dicts."},
    {"dumps", (PyCFunction)(void (*)(void))speedups_dumps, METH_VARARGS | METH_KEYWORDS,
     "Write a dict as a PHL document."},
    {"dumps_table", (PyCFunction)(void (*)(void))speedups_dumps_table, METH_VARARGS | METH_KEYWORDS,
     "Write a list of dicts with the same keys as an HPL table document."},
    {NULL, NULL, 0, NULL},
};

static struct PyModuleDef module = {
    PyModuleDef_HEAD_INIT, "phl._speedups", "The C version of the PHL parser and writer.", -1, methods, NULL, NULL, NULL, NULL,
};

PyMODINIT_FUNC PyInit__speedups(void) {
    PyObject *errors = PyImport_ImportModule("phl.errors");
    PyObject *abc;
    if (!errors) return NULL;
    phl_error_type = PyObject_GetAttrString(errors, "PHLError");
    Py_DECREF(errors);
    if (!phl_error_type) return NULL;

    abc = PyImport_ImportModule("collections.abc");
    if (!abc) return NULL;
    mapping_type = PyObject_GetAttrString(abc, "Mapping");
    Py_DECREF(abc);
    if (!mapping_type) return NULL;

    return PyModule_Create(&module);
}
