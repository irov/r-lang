#!/usr/bin/env python3
"""Differential tests of the xml example against Python's expat and ElementTree."""
import argparse
import random
import subprocess
import xml.etree.ElementTree as ElementTree
from xml.parsers import expat

SEED = 20260917


def escape(text):
    return text.replace('\\', '\\\\').replace('\t', '\\t').replace('\n', '\\n').replace('\r', '\\r')


def expat_events(document, keep_whitespace):
    """The event lines expat implies for a document, in the example's line format."""
    parser = expat.ParserCreate()
    parser.ordered_attributes = True
    lines = []
    depth = [0]
    pending = []
    in_cdata = [False]

    def flush():
        if pending:
            text = ''.join(pending)
            pending.clear()
            if keep_whitespace or text.strip():
                lines.append(f'text\t{depth[0]}\t\t{escape(text)}')

    def start(name, attributes):
        flush()
        depth[0] += 1
        pairs = ''.join(f'\t{escape(attributes[i])}={escape(attributes[i + 1])}' for i in range(0, len(attributes), 2))
        lines.append(f'start_element\t{depth[0]}\t{escape(name)}\t{pairs}')

    def end(name):
        flush()
        depth[0] -= 1
        lines.append(f'end_element\t{depth[0]}\t{escape(name)}\t')

    def data(text):
        pending.append(text)

    def comment(text):
        flush()
        lines.append(f'comment\t{depth[0]}\t\t{escape(text)}')

    def instruction(target, text):
        flush()
        lines.append(f'instruction\t{depth[0]}\t{escape(target)}\t{escape(text)}')

    def declaration(version, encoding, standalone):
        lines.append(f'declaration\t{depth[0]}\txml\t')

    def cdata_start():
        flush()
        in_cdata[0] = True

    def cdata_end():
        text = ''.join(pending)
        pending.clear()
        lines.append(f'cdata\t{depth[0]}\t\t{escape(text)}')
        in_cdata[0] = False

    parser.StartElementHandler = start
    parser.EndElementHandler = end
    parser.CharacterDataHandler = data
    parser.CommentHandler = comment
    parser.ProcessingInstructionHandler = instruction
    parser.XmlDeclHandler = declaration
    parser.StartCdataSectionHandler = cdata_start
    parser.EndCdataSectionHandler = cdata_end
    parser.Parse(document, True)
    flush()
    return '\n'.join(lines) + ('\n' if lines else '')


def generate(rng, depth=0):
    names = ['a', 'b', 'item', 'p:item', 'x-y', 'long_name']
    texts = ['alpha', 'beta &amp; gamma', 'x &lt; y &gt; z', 'caf\u00e9', '&#x41;&#66;', '\u4e2d\u6587', 'tab\there']
    parts = []
    name = rng.choice(names)
    attributes = ''
    used = set()
    for _ in range(rng.randint(0, 3)):
        key = rng.choice(['id', 'lang', 'p:kind', 'z'])
        if key in used:
            continue
        used.add(key)
        value = rng.choice(['1', 'two', 'a &amp; b', 'q&quot;q', 'caf\u00e9', 'x\ty'])
        quote = rng.choice(['"', "'"]) if '"' not in value else "'"
        attributes += f' {key}={quote}{value}{quote}'
    if depth > 3 or rng.random() < 0.2:
        return f'<{name}{attributes}/>'
    parts.append(f'<{name}{attributes}>')
    for _ in range(rng.randint(0, 4)):
        kind = rng.random()
        if kind < 0.35:
            parts.append(rng.choice(texts))
        elif kind < 0.7:
            parts.append(generate(rng, depth + 1))
        elif kind < 0.8:
            parts.append(f'<!-- note {rng.randint(0, 9)} -->')
        elif kind < 0.9:
            parts.append(f'<![CDATA[raw <{rng.randint(0, 9)}> data]]>')
        else:
            parts.append(f'<?proc data {rng.randint(0, 9)}?>')
        if rng.random() < 0.3:
            parts.append('\n  ')
    parts.append(f'</{name}>')
    return ''.join(parts)


def selected(document, pattern):
    """Independent evaluation of the streaming path subset over ElementTree."""
    steps = []
    absolute = pattern.startswith('/') and not pattern.startswith('//')
    rest = pattern[2:] if pattern.startswith('//') else pattern[1:] if pattern.startswith('/') else pattern
    descendant = not absolute
    while rest:
        if rest.startswith('/'):
            descendant = rest.startswith('//')
            rest = rest[2:] if descendant else rest[1:]
        name = ''
        while rest and rest[0] not in '/[':
            name += rest[0]
            rest = rest[1:]
        predicates = []
        while rest.startswith('['):
            close = rest.index(']')
            body = rest[2:close]
            if '=' in body:
                key, value = body.split('=', 1)
                predicates.append((key, value[1:-1]))
            else:
                predicates.append((body, None))
            rest = rest[close + 1:]
        steps.append((name, descendant, predicates))
    root = ElementTree.fromstring(document)
    results = []
    ordinal = [0]

    def walk(element, path):
        ordinal[0] += 1
        path = path + [element]
        names = [e.tag for e in path]
        reached = [False] * len(path)
        for index, (name, desc, predicates) in enumerate(steps):
            nxt = [False] * len(path)
            for at in range(len(path)):
                if index == 0:
                    allowed = (not absolute) or at == 0
                elif desc:
                    allowed = any(reached[:at])
                else:
                    allowed = at > 0 and reached[at - 1]
                nxt[at] = allowed and (name == '*' or names[at] == name)
            reached = nxt
        match = reached[-1]
        for key, value in steps[-1][2]:
            if key not in element.attrib or (value is not None and element.attrib[key] != value):
                match = False
        if match:
            results.append((ordinal[0], len(path)))
        for child in element:
            walk(child, path)

    walk(root, [])
    return results


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--executable', required=True)
    exe = parser.parse_args().executable
    rng = random.Random(SEED)
    count = 0

    def run(args, data, status=0):
        nonlocal count
        result = subprocess.run([exe, *args], input=data, capture_output=True, timeout=120)
        assert result.returncode == status, (args, result.returncode, result.stderr, data[:200])
        if status == 0:
            assert not result.stderr, (args, result.stderr)
        count += 1
        return result

    documents = ["<?xml version='1.0' encoding='utf-8'?>\n<!-- head -->\n" + generate(rng) + "\n<?tail x?>\n" for _ in range(24)]
    documents += ['<a/>', '<a>\n</a>', "<a xmlns='urn:a' xmlns:p='urn:p'><p:b p:c='1'>t</p:b></a>"]
    for document in documents:
        data = document.encode()
        expected = expat_events(data, False)
        for chunk in (1, 5, 4096):
            actual = run(['events', str(chunk)], data).stdout.decode()
            assert actual == expected, (chunk, document, actual, expected)
        assert run(['events_all'], data).stdout.decode() == expat_events(data, True), document
        # Rewrites carry a fresh declaration; everything else parses back identically.
        undeclared = ''.join(line + '\n' for line in expected.splitlines() if not line.startswith('declaration'))
        for form in ('compact', 'pretty'):
            rewritten = run([form], data).stdout
            events = expat_events(rewritten, False)
            assert events.startswith('declaration'), (form, rewritten)
            assert ''.join(line + '\n' for line in events.splitlines()[1:]) == undeclared, (form, document, rewritten)

    # Selectors against an independent evaluation.
    tree = "<root a='1'><item id='1'><name>x</name></item><item id='2' lang='en'><name/><deep><item id='3'/></deep></item><other><name/></other></root>"
    for pattern in ('/root/item', '//item', 'item/name', '/root/*/name', "//item[@id='3']", '//item[@lang]', '/item', 'name', '//*', "*[@id='2']"):
        lines = run(['select', pattern, '3'], tree.encode()).stdout.decode().splitlines()
        actual = [(int(line.split('\t')[0]), int(line.split('\t')[1])) for line in lines]
        assert actual == selected(tree, pattern), (pattern, actual, selected(tree, pattern))
    namespaced = run(['select', '//p:b'], "<a xmlns='urn:a' xmlns:p='urn:p'><p:b>t</p:b></a>".encode()).stdout.decode()
    assert namespaced == '2\t2\t/a/p:b\tb\turn:p\n', namespaced

    # Errors name their code and offset and never crash.
    assert b'unbalanced' in run(['events'], b'<a><b></a>', 65).stderr
    assert b'unsupported' in run(['events'], b'<!DOCTYPE a><a/>', 65).stderr
    assert b'unsupported' in run(['events'], b'<a>&nbsp;</a>', 65).stderr
    assert b'malformed' in run(['events'], b'<a/><b/>', 65).stderr
    assert b'duplicate_attribute' in run(['events'], b"<a x='1' x='2'/>", 65).stderr
    assert b'invalid_utf8' in run(['events'], b'<a>\xff</a>', 65).stderr
    assert b'invalid_selector' in run(['select', 'a[@x]/b'], b'<a/>', 65).stderr
    assert run(['select'], b'<a/>', 64).returncode == 64
    assert run(['events', 'zero'], b'<a/>', 64).returncode == 64
    assert run(['squash'], b'<a/>', 64).returncode == 64
    print(f'xml example: {count} command checks passed')


if __name__ == '__main__':
    main()
