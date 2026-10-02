#!/usr/bin/env python3
"""std.json::schema::<T>() (R-SLIB-JSON-0002) against the values that std.json::marshal writes.

The driver prints `schema TAG JSON` and `value TAG JSON` lines. Every value shall validate against
the schema of its tag, and values altered in ways the decoder rejects shall not. The validator
below covers the JSON Schema 2020-12 keywords the compiler writes; when the `jsonschema` package
is installed, its Draft 2020-12 validator is consulted as well."""
import argparse
import copy
import json
import subprocess
import sys

parser = argparse.ArgumentParser()
parser.add_argument('--executable', required=True)
args = parser.parse_args()

WRITTEN = {'type', 'properties', 'required', 'additionalProperties', 'items', 'minItems',
           'maxItems', 'minLength', 'maxLength', 'minimum', 'maximum', 'enum', 'anyOf', 'oneOf',
           '$ref', '$defs', 'description'}


def resolve(root, reference):
    if reference == '#':
        return root
    prefix = '#/$defs/'
    assert reference.startswith(prefix), reference
    return root['$defs'][reference[len(prefix):]]


def type_matches(expected, value):
    if expected == 'null':
        return value is None
    if expected == 'boolean':
        return isinstance(value, bool)
    if expected == 'integer':
        return isinstance(value, int) and not isinstance(value, bool)
    if expected == 'number':
        return isinstance(value, (int, float)) and not isinstance(value, bool)
    if expected == 'string':
        return isinstance(value, str)
    if expected == 'array':
        return isinstance(value, list)
    if expected == 'object':
        return isinstance(value, dict)
    raise AssertionError(expected)


def valid(schema, value, root):
    if schema is True or schema == {}:
        return True
    if schema is False:
        return False
    unknown = set(schema) - WRITTEN
    assert not unknown, unknown
    if '$ref' in schema and not valid(resolve(root, schema['$ref']), value, root):
        return False
    if 'anyOf' in schema and not any(valid(s, value, root) for s in schema['anyOf']):
        return False
    if 'oneOf' in schema and sum(1 for s in schema['oneOf'] if valid(s, value, root)) != 1:
        return False
    if 'enum' in schema and value not in schema['enum']:
        return False
    if 'type' in schema and not type_matches(schema['type'], value):
        return False
    if isinstance(value, (int, float)) and not isinstance(value, bool):
        if 'minimum' in schema and value < schema['minimum']:
            return False
        if 'maximum' in schema and value > schema['maximum']:
            return False
    if isinstance(value, str):
        if 'minLength' in schema and len(value) < schema['minLength']:
            return False
        if 'maxLength' in schema and len(value) > schema['maxLength']:
            return False
    if isinstance(value, list):
        if 'minItems' in schema and len(value) < schema['minItems']:
            return False
        if 'maxItems' in schema and len(value) > schema['maxItems']:
            return False
        if 'items' in schema and not all(valid(schema['items'], item, root) for item in value):
            return False
    if isinstance(value, dict):
        properties = schema.get('properties', {})
        if any(name not in value for name in schema.get('required', [])):
            return False
        for name, item in value.items():
            if name in properties:
                if not valid(properties[name], item, root):
                    return False
            elif 'additionalProperties' in schema and not valid(
                    schema['additionalProperties'], item, root):
                return False
    return True


try:
    import jsonschema
except ImportError:
    jsonschema = None


def check(schema, value, expected, note):
    outcome = valid(schema, value, schema)
    assert outcome == expected, (note, value, outcome)
    if jsonschema is not None:
        library = jsonschema.Draft202012Validator(schema).is_valid(value)
        assert library == expected, ('jsonschema', note, value, library)


result = subprocess.run([args.executable], capture_output=True, timeout=120)
assert result.returncode == 0, result
schemas, values = {}, {}
for line in result.stdout.decode().splitlines():
    kind, tag, text = line.split(' ', 2)
    document = json.loads(text)
    if kind == 'schema':
        schemas[tag] = document
    else:
        values.setdefault(tag, []).append(document)

checks = 0
if jsonschema is not None:
    for document in schemas.values():
        jsonschema.Draft202012Validator.check_schema(document)
        checks += 1
for tag, items in values.items():
    for item in items:
        check(schemas[tag], item, True, tag)
        checks += 1

args_schema = schemas['args']
assert args_schema['type'] == 'object'
assert args_schema['properties']['query']['description'] == 'What to look for'
assert args_schema['properties']['limit']['description'] == 'How many "results"'
assert 'limit' not in args_schema['required'] and 'query' in args_schema['required']
assert 'hidden' not in args_schema['properties']
assert args_schema['properties']['big'] == {'type': 'string'}
first = values['args'][0]
mutations = [
    ('missing required', lambda v: v.pop('query')),
    ('wrong string type', lambda v: v.__setitem__('query', 5)),
    ('integer above u16', lambda v: v.__setitem__('limit', 65536)),
    ('integer below i8', lambda v: v['weights'].append(-129)),
    ('fraction for integer', lambda v: v.__setitem__('limit', 1.5)),
    ('fixed array too short', lambda v: v['rgb'].pop()),
    ('fixed array too long', lambda v: v['rgb'].append(1)),
    ('unknown enum name', lambda v: v.__setitem__('color', 'purple')),
    ('payload variant with extra member', lambda v: v['shape'].__setitem__('square', None)),
    ('unit variant as object', lambda v: v.__setitem__('shape', {'square': 1})),
    ('two characters for char', lambda v: v.__setitem__('initial', 'ab')),
    ('byte above 255', lambda v: v['blob'].append(256)),
    ('number for quoted integer', lambda v: v.__setitem__('big', 1)),
    ('string for number in dictionary', lambda v: v['scores'].__setitem__('b', 'x')),
    ('boolean or null expected', lambda v: v.__setitem__('exact', 'yes')),
    ('generic instance field', lambda v: v['range'].__setitem__('left', -1)),
]
for note, mutate in mutations:
    altered = copy.deepcopy(first)
    mutate(altered)
    check(args_schema, altered, False, note)
    checks += 1
optional_absent = copy.deepcopy(first)
optional_absent.pop('limit')
check(args_schema, optional_absent, True, 'optional field absent')
checks += 1

extended = schemas['extended']
assert extended['required'] == ['id', 'title']
extended_value = values['extended'][0]
check(extended, dict(extended_value, y=7), True, 'collector accepts an integer')
check(extended, dict(extended_value, y='seven'), False, 'collector rejects a string')
checks += 2

node = schemas['node']
assert node['properties']['children']['items'] == {'$ref': '#'}
check(node, {'name': 'a', 'children': [{'name': 'b', 'children': []}]}, True, 'recursive tree')
check(node, {'name': 'a', 'children': [{'name': 'b'}]}, False, 'recursive tree missing children')
checks += 2
check(schemas['colors'], ['green'], True, 'array of enum')
check(schemas['colors'], ['green', 1], False, 'array of enum with a number')
checks += 2
celsius = schemas['celsius']
assert set(celsius) == {'$ref', '$defs'}, celsius
assert celsius['$ref'].startswith('#/$defs/'), celsius
assert list(celsius['$defs'].values()) == [
    {'type': 'string', 'minLength': 2, 'description': 'degrees Celsius, such as 21C'}], celsius
assert args_schema['properties']['outside'] == {'$ref': celsius['$ref']}, args_schema['properties']['outside']
check(celsius, '-5C', True, 'a hooked schema')
check(celsius, 5, False, 'a hooked schema rejects a number')
check(args_schema, dict(first, outside='x'), False, 'hooked field too short')
checks += 3
print(f'{checks} JSON Schema checks passed'
      + ('' if jsonschema is None else ' (also with jsonschema)'))
