#!/usr/bin/env python3
"""An MCP server of notes for an assistant (std.mcp over standard input and output and over
Streamable HTTP, std.json::schema, std.jsonrpc, the Tasks extension). The client of the same
process converses with it over both transports; this script speaks the 2026-07-28 revision to it
by hand over a pipe and over HTTP, with and without the Tasks extension, replays a recorded
session and checks the JSON-RPC analyzer of the example."""
import argparse
import http.client
import json
import os
import select
import socket
import subprocess
import tempfile
import time

parser = argparse.ArgumentParser()
parser.add_argument('--executable', required=True)
args = parser.parse_args()
usage = ('assistant demo\nassistant serve\nassistant serve-http PORT\nassistant replay FILE\n'
         'assistant schema remember|recall|forget\nassistant inspect URL [TOOL JSON]\n'
         'assistant inspect-stdio PROGRAM [ARGUMENT...] [-- TOOL JSON]\nassistant frames\n')
VERSION = '2026-07-28'
SERVER_INFO = {'name': 'assistant-memory', 'version': '0.1.0'}


def run(values):
    result = subprocess.run([args.executable, *values], capture_output=True, timeout=120)
    return result.returncode, result.stdout.decode(), result.stderr.decode()


checks = 0


def expect(values, status, stdout, stderr=''):
    global checks
    outcome = run(values)
    assert outcome == (status, stdout, stderr), (values, outcome)
    checks += 1


def check(condition, *context):
    global checks
    assert condition, context
    checks += 1


expect([], 0, '', usage)
for wrong in [['demo', 'extra'], ['serve', 'extra'], ['serve-http', '0'], ['serve-http', '70000'],
              ['serve-http', 'x'], ['schema', 'nope'], ['inspect'], ['inspect', 'a', 'b'], ['inspect-stdio'],
              ['replay'], ['replay', 'a', 'b'], ['frames', 'extra']]:
    expect(wrong, 64, '', usage)


def conversation(label, ending):
    return (f'{label} discover assistant-memory 0.1.0 2026-07-28 tasks\n'
            f'{label} tools remember recall forget count\n'
            f'{label} remember {{"id":1}}\n'
            f'{label} remember {{"id":2}}\n'
            f'{label} listen resources memory://notes\n'
            f'{label} remember {{"id":3}}\n'
            f'{label} change resources\n'
            f'{label} change updated memory://notes\n'
            f'{label} recall {{"notes":[{{"id":1,"text":"tea with Ann on Friday"}},{{"id":2,"text":"green tea"}}]}}\n'
            '  asks: Forget note 2: green tea?\n'
            f'{label} forget forgot note 2\n'
            f'{label} read memory://notes/1 tea with Ann on Friday\n'
            f'{label} read memory://notes/2 -32602 Resource not found\n'
            f'{label} resources memory://notes memory://notes/1 memory://notes/3\n'
            f'{label} prompt What do I know about tea? Note: tea with Ann on Friday.\n'
            f'{label} complete t -> tea the trip\n'
            f'{label} count 10 words\n'
            f'{label} task forget working\n'
            f'{label} task input_required Forget note 3: call Bob about the trip?\n'
            f'{label} listen tasks 1\n'
            f'{label} task completed forgot note 3\n'
            f'{label} task forget working\n'
            f'{label} task input_required Forget note 1: tea with Ann on Friday?\n'
            f'{label} task cancelled\n'
            f'{label} {ending}\n')


# The HTTP server of demo is protected: without a token the client is refused with 401, with the
# read token its call of a tool with 403, and the demo token may do everything.
demo = ('http discover refused: unauthorized\n'
        'http remember refused: forbidden\n' +
        conversation('http', 'stopped') + conversation('stdio', 'server exited 0'))
for attempt in range(3):
    expect(['demo'], 0, demo)

# The schemas of the arguments come from the R types and their @json attributes.
SCHEMAS = {
    'remember': {'type': 'object', 'properties': {
        'text': {'type': 'string', 'description': 'What to remember'}}, 'required': ['text']},
    'recall': {'type': 'object', 'properties': {
        'query': {'type': 'string', 'description': 'Words to look for, in any case'},
        'limit': {'type': 'integer', 'minimum': 0, 'maximum': 4294967295,
                  'description': 'The most notes to return'}}, 'required': ['query']},
    'forget': {'type': 'object', 'properties': {
        'id': {'type': 'integer', 'minimum': 0, 'maximum': 18446744073709551615,
               'description': 'The id of the note'}}, 'required': ['id']},
}
for tool, schema in SCHEMAS.items():
    code, out, err = run(['schema', tool])
    check(code == 0 and err == '' and json.loads(out) == schema, tool, code, out, err)


def listing(label, notes=0):
    return (f'{label} server assistant-memory 0.1.0 speaks 2026-07-28\n'
            f'{label} tool remember: Keeps a note\n'
            f'{label} tool recall: Finds the notes that contain the query\n'
            f'{label} tool forget: Forgets a note after the user confirms it\n'
            f'{label} tool count: Counts the words of the notes\n'
            f'{label} resource memory://notes\n' +
            ''.join(f'{label} resource memory://notes/{id}\n' for id in range(1, notes + 1)) +
            f'{label} prompt reflect topic*\n')


server = [args.executable, 'serve']
expect(['inspect-stdio', *server], 0, listing('stdio'))
expect(['inspect-stdio', *server, '--', 'remember', '{"text":"milk"}'], 0,
       listing('stdio') + 'stdio remember {"id":1}\n')
expect(['inspect-stdio', *server, '--', 'remember', '{"txt":"milk"}'], 0,
       listing('stdio') + 'stdio remember remember needs a text\n')
expect(['inspect-stdio', *server, '--', 'nothing', '{}'], 1, listing('stdio'),
       'mcp error -32602 (invalid params): Unknown tool: nothing\n')


def meta(capabilities=None, token=None):
    value = {'io.modelcontextprotocol/protocolVersion': VERSION,
             'io.modelcontextprotocol/clientCapabilities': capabilities or {},
             'io.modelcontextprotocol/clientInfo': {'name': 'behaviour', 'version': '1'}}
    if token is not None:
        value['progressToken'] = token
    return value


def request(ident, method, params=None, capabilities=None, token=None):
    body = dict(params or {})
    body['_meta'] = meta(capabilities, token)
    return {'jsonrpc': '2.0', 'id': ident, 'method': method, 'params': body}


def text_of(result):
    return result['content'][0]['text']


FORM = {'elicitation': {'form': {}}}
EXTENSIONS = {'io.modelcontextprotocol/tasks': {}}
TASKS = {'extensions': EXTENSIONS}
TASKS_FORM = {'elicitation': {'form': {}}, 'extensions': EXTENSIONS}


class Pipe:
    """The standard input and output of a stdio server, one JSON message per line."""

    def __init__(self):
        self.process = subprocess.Popen(server, stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                                        stderr=subprocess.PIPE)
        self.pending = b''

    def send(self, message):
        self.process.stdin.write(json.dumps(message).encode() + b'\n')
        self.process.stdin.flush()

    def line(self):
        deadline = time.monotonic() + 60
        while b'\n' not in self.pending:
            left = deadline - time.monotonic()
            assert left > 0, 'no line from the stdio server'
            ready, _, _ = select.select([self.process.stdout], [], [], left)
            if ready:
                piece = os.read(self.process.stdout.fileno(), 65536)
                assert piece, 'the stdio server closed its output'
                self.pending += piece
        line, self.pending = self.pending.split(b'\n', 1)
        return json.loads(line)

    def exchange(self, message):
        """The response to a request and the notifications that came before it."""
        self.send(message)
        notices = []
        while True:
            got = self.line()
            if 'id' in got and got['id'] == message['id'] and 'method' not in got:
                return got, notices
            notices.append(got)

    def finish(self):
        self.process.stdin.close()
        rest = self.process.stdout.read()
        error = self.process.stderr.read()
        return self.process.wait(timeout=60), rest, error


pipe = Pipe()
try:
    found, _ = pipe.exchange(request(1, 'server/discover'))
    result = found['result']
    check(result['supportedVersions'] == [VERSION] and result['resultType'] == 'complete', found)
    check(result['_meta']['io.modelcontextprotocol/serverInfo'] == SERVER_INFO, found)
    check(result['instructions'] == 'Keeps short notes for an assistant: remember, recall and forget them.',
          found)
    check(result['capabilities']['tools'] == {'listChanged': True} and
          result['capabilities']['resources'] == {'listChanged': True, 'subscribe': True} and
          'completions' in result['capabilities'], found)
    listed, _ = pipe.exchange(request(2, 'tools/list'))
    tools = listed['result']['tools']
    check([tool['name'] for tool in tools] == ['remember', 'recall', 'forget', 'count'], listed)
    check(all(tools[index]['inputSchema'] == SCHEMAS[name]
              for index, name in enumerate(['remember', 'recall', 'forget'])), tools)
    check(tools[3]['inputSchema'] == {'type': 'object', 'additionalProperties': False}, tools[3])
    check([tool['annotations'] for tool in tools] == [
        {'readOnlyHint': False, 'destructiveHint': False, 'idempotentHint': False, 'openWorldHint': False},
        {'readOnlyHint': True, 'destructiveHint': False, 'idempotentHint': True, 'openWorldHint': False},
        {'readOnlyHint': False, 'destructiveHint': True, 'idempotentHint': True, 'openWorldHint': False},
        {'readOnlyHint': True, 'destructiveHint': False, 'idempotentHint': True, 'openWorldHint': False}], tools)
    check(tools[0]['outputSchema']['properties']['id']['description'] == 'The id of the new note', tools[0])
    check(listed['result']['ttlMs'] == 1000, listed)
    for ident, text in [(3, 'buy milk'), (4, 'milk and tea'), (5, 'call Bob')]:
        added, _ = pipe.exchange(request(ident, 'tools/call', {'name': 'remember', 'arguments': {'text': text}}))
        check(added['result']['structuredContent'] == {'id': ident - 2} and
              json.loads(text_of(added['result'])) == {'id': ident - 2}, added)
    wrong, _ = pipe.exchange(request(6, 'tools/call', {'name': 'remember', 'arguments': {'txt': 1}}))
    check(wrong['result']['isError'] is True and text_of(wrong['result']) == 'remember needs a text', wrong)
    recalled, _ = pipe.exchange(request(7, 'tools/call', {'name': 'recall', 'arguments': {'query': 'MILK', 'limit': 1}}))
    check(recalled['result']['structuredContent'] == {'notes': [{'id': 1, 'text': 'buy milk'}]}, recalled)
    unknown, _ = pipe.exchange(request(8, 'tools/call', {'name': 'nothing', 'arguments': {}}))
    check(unknown['error']['code'] == -32602 and unknown['error']['message'] == 'Unknown tool: nothing', unknown)
    # forget asks for a confirmation through a form: a client without elicitation gets -32021,
    # a client with it gets the input request and retries with the answer and the state.
    refused, _ = pipe.exchange(request(9, 'tools/call', {'name': 'forget', 'arguments': {'id': 1}}))
    check(refused['error']['code'] == -32021 and
          refused['error']['data'] == {'requiredCapabilities': {'elicitation': {'form': {}}}}, refused)
    asked, _ = pipe.exchange(request(10, 'tools/call', {'name': 'forget', 'arguments': {'id': 1}}, FORM))
    result = asked['result']
    check(result['resultType'] == 'input_required' and result['requestState'] == 'forget:1', asked)
    question = result['inputRequests']['confirm']
    check(question['method'] == 'elicitation/create' and question['params']['mode'] == 'form' and
          question['params']['message'] == 'Forget note 1: buy milk?' and
          question['params']['requestedSchema']['properties']['confirm']['type'] == 'boolean', question)
    declined, _ = pipe.exchange(request(11, 'tools/call', {
        'name': 'forget', 'arguments': {'id': 1}, 'requestState': 'forget:1',
        'inputResponses': {'confirm': {'action': 'decline'}}}, FORM))
    check(text_of(declined['result']) == 'kept note 1', declined)
    # A state of another note asks again instead of forgetting this one.
    again, _ = pipe.exchange(request(12, 'tools/call', {
        'name': 'forget', 'arguments': {'id': 1}, 'requestState': 'forget:2',
        'inputResponses': {'confirm': {'action': 'accept', 'content': {'confirm': True}}}}, FORM))
    check(again['result']['resultType'] == 'input_required', again)
    confirmed, _ = pipe.exchange(request(13, 'tools/call', {
        'name': 'forget', 'arguments': {'id': 1}, 'requestState': 'forget:1',
        'inputResponses': {'confirm': {'action': 'accept', 'content': {'confirm': True}}}}, FORM))
    check(text_of(confirmed['result']) == 'forgot note 1', confirmed)
    malformed, _ = pipe.exchange(request(14, 'tools/call', {
        'name': 'forget', 'arguments': {'id': 2}, 'inputResponses': {'confirm': {'action': 'maybe'}}}, FORM))
    check(malformed['error']['code'] == -32602, malformed)
    counted, notices = pipe.exchange(request(15, 'tools/call', {'name': 'count', 'arguments': {}}, token='count'))
    check(text_of(counted['result']) == '5 words', counted)
    check([(notice['method'], notice['params']['progressToken'], notice['params']['progress'],
            notice['params']['total']) for notice in notices] ==
          [('notifications/progress', 'count', 1, 2), ('notifications/progress', 'count', 2, 2)], notices)
    read, _ = pipe.exchange(request(16, 'resources/read', {'uri': 'memory://notes/2'}))
    check(read['result']['contents'] == [{'uri': 'memory://notes/2', 'mimeType': 'text/plain',
                                          'text': 'milk and tea'}], read)
    missing, _ = pipe.exchange(request(17, 'resources/read', {'uri': 'memory://notes/1'}))
    check(missing['error']['code'] == -32602 and missing['error']['message'] == 'Resource not found', missing)
    resources, _ = pipe.exchange(request(18, 'resources/list'))
    check([item['uri'] for item in resources['result']['resources']] ==
          ['memory://notes', 'memory://notes/2', 'memory://notes/3'], resources)
    templates, _ = pipe.exchange(request(19, 'resources/templates/list'))
    check(templates['result']['resourceTemplates'] ==
          [{'uriTemplate': 'memory://notes/{id}', 'name': 'note', 'mimeType': 'text/plain'}], templates)
    prompt, _ = pipe.exchange(request(20, 'prompts/get', {'name': 'reflect', 'arguments': {'topic': 'tea'}}))
    check(prompt['result']['description'] == 'Reflects on the notes about a topic' and
          prompt['result']['messages'] == [{'role': 'user', 'content': {
              'type': 'text', 'text': 'What do I know about tea? Note: milk and tea.'}}], prompt)
    completed, _ = pipe.exchange(request(21, 'completion/complete', {
        'ref': {'type': 'ref/prompt', 'name': 'reflect'}, 'argument': {'name': 'topic', 'value': 'm'}}))
    check(completed['result']['completion']['values'] == ['milk'], completed)
    removed, _ = pipe.exchange(request(22, 'initialize'))
    check(removed['error']['code'] == -32601, removed)
    old = request(23, 'tools/list')
    old['params']['_meta']['io.modelcontextprotocol/protocolVersion'] = '2025-11-25'
    rejected, _ = pipe.exchange(old)
    check(rejected['error']['code'] == -32022 and
          rejected['error']['data'] == {'supported': [VERSION], 'requested': '2025-11-25'}, rejected)
    # The Tasks extension: forget and count run as tasks for a client that declares it, and the
    # client polls them with tasks/get, answers their input with tasks/update and cancels them.
    check(found['result']['capabilities']['extensions'] == EXTENSIONS, found)

    def poll(ident, task_id, status):
        for attempt in range(300):
            got, _ = pipe.exchange(request(ident, 'tasks/get', {'taskId': task_id}, TASKS))
            if got['result']['status'] == status:
                return got['result']
            time.sleep(0.02)
        raise AssertionError(('the task did not reach', status, got))

    created, _ = pipe.exchange(request(30, 'tools/call', {'name': 'count', 'arguments': {}}, TASKS))
    task = created['result']
    check(task['resultType'] == 'task' and task['status'] == 'working' and task['ttlMs'] == 600000 and
          task['pollIntervalMs'] == 50 and len(task['taskId']) == 36 and task['createdAt'].endswith('Z') and
          task['lastUpdatedAt'] == task['createdAt'], created)
    counted = poll(31, task['taskId'], 'completed')
    check(text_of(counted['result']) == '5 words' and counted['statusMessage'] == 'counted a note' and
          counted['resultType'] == 'complete', counted)
    created, _ = pipe.exchange(request(32, 'tools/call', {'name': 'forget', 'arguments': {'id': 3}}, TASKS_FORM))
    task_id = created['result']['taskId']
    asking = poll(33, task_id, 'input_required')
    check(list(asking['inputRequests']) == ['1.confirm'] and
          asking['inputRequests']['1.confirm']['params']['message'] == 'Forget note 3: call Bob?', asking)
    taken, _ = pipe.exchange(request(34, 'tasks/update', {
        'taskId': task_id, 'inputResponses': {'1.confirm': {'action': 'accept', 'content': {'confirm': True}}}}, TASKS))
    check(taken['result']['resultType'] == 'complete', taken)
    done = poll(35, task_id, 'completed')
    check(text_of(done['result']) == 'forgot note 3' and 'inputRequests' not in done, done)
    unknown, _ = pipe.exchange(request(36, 'tasks/cancel', {'taskId': 'nope'}, TASKS))
    check(unknown['error'] == {'code': -32602, 'message': 'Failed to retrieve task: Task not found'}, unknown)
    undeclared, _ = pipe.exchange(request(37, 'tasks/get', {'taskId': task_id}))
    check(undeclared['error']['code'] == -32021 and
          undeclared['error']['data'] == {'requiredCapabilities': {'extensions': EXTENSIONS}}, undeclared)
    created, _ = pipe.exchange(request(38, 'tools/call', {'name': 'forget', 'arguments': {'id': 2}}, TASKS_FORM))
    task_id = created['result']['taskId']
    poll(39, task_id, 'input_required')
    cancelled, _ = pipe.exchange(request(40, 'tasks/cancel', {'taskId': task_id}, TASKS))
    check(cancelled['result']['resultType'] == 'complete', cancelled)
    state, _ = pipe.exchange(request(41, 'tasks/get', {'taskId': task_id}, TASKS))
    check(state['result']['status'] == 'cancelled' and 'inputRequests' not in state['result'], state)
    kept, _ = pipe.exchange(request(42, 'resources/read', {'uri': 'memory://notes/2'}))
    check(kept['result']['contents'][0]['text'] == 'milk and tea', kept)
finally:
    status, rest, error = pipe.finish()
check((status, rest, error) == (0, b'', b''), status, rest, error)


def free_port():
    with socket.socket() as probe:
        probe.bind(('127.0.0.1', 0))
        return probe.getsockname()[1]


def post(port, message, headers=None, method=None, name=None):
    """A POST to /mcp with the headers of the revision; the status, headers and body."""
    sent = {'Content-Type': 'application/json', 'Accept': 'application/json, text/event-stream',
            'MCP-Protocol-Version': VERSION, 'Mcp-Method': method or message['method']}
    if name is not None:
        sent['Mcp-Name'] = name
    sent.update(headers or {})
    connection = http.client.HTTPConnection('127.0.0.1', port, timeout=60)
    try:
        connection.request('POST', '/mcp', json.dumps(message).encode(), sent)
        answer = connection.getresponse()
        return answer.status, answer.getheader('Content-Type'), answer.read()
    finally:
        connection.close()


def events(body):
    return [json.loads(line[len('data: '):]) for line in body.decode().split('\n') if line.startswith('data: ')]


def first_event(port, message):
    """The status of a POST that opens an event stream and the first message of the stream."""
    sent = {'Content-Type': 'application/json', 'Accept': 'application/json, text/event-stream',
            'MCP-Protocol-Version': VERSION, 'Mcp-Method': message['method']}
    connection = http.client.HTTPConnection('127.0.0.1', port, timeout=60)
    try:
        connection.request('POST', '/mcp', json.dumps(message).encode(), sent)
        answer = connection.getresponse()
        if answer.getheader('Content-Type') != 'text/event-stream':
            return answer.status, json.loads(answer.read())
        while True:
            line = answer.readline().decode()
            assert line, 'the event stream ended'
            if line.startswith('data: '):
                return answer.status, json.loads(line[len('data: '):])
    finally:
        connection.close()


port = free_port()
served = subprocess.Popen([args.executable, 'serve-http', str(port)], stdout=subprocess.PIPE,
                          stderr=subprocess.PIPE)
try:
    announced = served.stderr.readline().decode()
    check(announced == f'serving MCP 2026-07-28 at http://127.0.0.1:{port}/mcp\n', announced)
    status, kind, body = post(port, request(1, 'tools/list'))
    check(status == 200 and kind == 'application/json', status, kind, body)
    check([tool['name'] for tool in json.loads(body)['result']['tools']] ==
          ['remember', 'recall', 'forget', 'count'], body)
    for ident, text in [(2, 'tea with Ann'), (3, 'green tea and cake')]:
        status, kind, body = post(port, request(ident, 'tools/call', {'name': 'remember', 'arguments': {'text': text}}),
                                  name='remember')
        check(status == 200 and json.loads(body)['result']['structuredContent'] == {'id': ident - 1}, status, body)
    status, kind, body = post(port, request(4, 'tools/call', {'name': 'remember', 'arguments': {'text': 'x'}}),
                              name='recall')
    check(status == 400 and json.loads(body)['error']['code'] == -32020, status, body)
    status, kind, body = post(port, request(5, 'tools/list'), method='tools/call')
    check(status == 400 and json.loads(body)['error']['code'] == -32020, status, body)
    status, kind, body = post(port, request(6, 'tools/list'), {'Origin': 'https://elsewhere.example'})
    check(status == 403, status, body)
    status, kind, body = post(port, request(7, 'tools/list'), {'Content-Type': 'text/plain'})
    check(status == 415, status, body)
    status, kind, body = post(port, {'jsonrpc': '2.0', 'method': 'notifications/cancelled',
                                     'params': {'requestId': 1}})
    check(status == 202 and body == b'', status, body)
    connection = http.client.HTTPConnection('127.0.0.1', port, timeout=60)
    connection.request('GET', '/mcp', headers={'Accept': 'text/event-stream'})
    answer = connection.getresponse()
    check(answer.status == 405, answer.status)
    answer.read()
    connection.close()
    # The progress of count arrives as events of a stream that ends with the result.
    status, kind, body = post(port, request(8, 'tools/call', {'name': 'count', 'arguments': {}}, token=8),
                              name='count')
    check(status == 200 and kind == 'text/event-stream', status, kind, body)
    streamed = events(body)
    check([event.get('method') for event in streamed] ==
          ['notifications/progress', 'notifications/progress', None], streamed)
    check(text_of(streamed[2]['result']) == '7 words' and streamed[2]['id'] == 8, streamed)
    status, kind, body = post(port, request(9, 'tools/call', {'name': 'forget', 'arguments': {'id': 2}}, FORM),
                              name='forget')
    check(status == 200 and json.loads(body)['result']['requestState'] == 'forget:2', status, body)
    status, kind, body = post(port, request(10, 'resources/read', {'uri': 'memory://notes'}),
                              name='memory://notes')
    read = json.loads(body)['result']
    check(status == 200 and json.loads(read['contents'][0]['text']) ==
          {'notes': [{'id': 1, 'text': 'tea with Ann'}, {'id': 2, 'text': 'green tea and cake'}]} and
          read['ttlMs'] == 1000, status, body)
    address = f'http://127.0.0.1:{port}/mcp'
    expect(['inspect', address], 0, listing('http', 2))
    expect(['inspect', address, 'recall', '{"query":"cake"}'], 0,
           listing('http', 2) + 'http recall {"notes":[{"id":2,"text":"green tea and cake"}]}\n')
    expect(['inspect', f'http://127.0.0.1:{port}/other'], 1, '',
           'mcp error 3 (protocol violation): the server answered with HTTP status 404 and no message\n')
    # A task over HTTP: tasks/get names the task in Mcp-Name, and a subscription to it lists it in
    # its acknowledgement.
    status, kind, body = post(port, request(11, 'tools/call', {'name': 'count', 'arguments': {}}, TASKS), name='count')
    created = json.loads(body)['result']
    check(status == 200 and created['resultType'] == 'task', status, body)
    task_id = created['taskId']
    for attempt in range(300):
        status, kind, body = post(port, request(12, 'tasks/get', {'taskId': task_id}, TASKS), name=task_id)
        state = json.loads(body)['result']
        if state['status'] == 'completed':
            break
        time.sleep(0.02)
    check(status == 200 and text_of(state['result']) == '7 words', status, body)
    status, kind, body = post(port, request(13, 'tasks/get', {'taskId': task_id}, TASKS), name='other')
    check(status == 400 and json.loads(body)['error']['code'] == -32020, status, body)
    status, acknowledged = first_event(port, request(14, 'subscriptions/listen', {
        'notifications': {'taskIds': [task_id, 'unknown']}}, TASKS))
    check(status == 200 and acknowledged['method'] == 'notifications/subscriptions/acknowledged' and
          acknowledged['params']['notifications'] == {'taskIds': [task_id]}, status, acknowledged)
    status, refused = first_event(port, request(15, 'subscriptions/listen', {'notifications': {'taskIds': [task_id]}}))
    check(refused['error']['code'] == -32021, status, refused)
finally:
    served.terminate()
    served.wait(timeout=60)
    served.stdout.close()
    served.stderr.close()

def canonical(message):
    return json.dumps(message, separators=(',', ':'), ensure_ascii=False)


# frames: every line in its canonical form, or the error response for a line that is no message.
lines = ['{"jsonrpc":"2.0","id":1,"method":"tools/list"}',
         '{"jsonrpc": "2.0", "id": "a", "method": "x", "params": {}}',
         '',
         'not json',
         '{"jsonrpc":"2.0","method":"notifications/progress","params":{"progress":1}}',
         '{"jsonrpc":"2.0","id":7,"result":{}}',
         '{"jsonrpc":"2.0","id":8,"error":{"code":-32602,"message":"Unknown tool: x"}}',
         '[1]',
         '{"jsonrpc":"2.0","id":null,"method":"m"}',
         '{"jsonrpc":"1.0","id":9,"method":"m"}',
         '{"jsonrpc":"2.0","id":10,"method":"m","params":3}']
parse_error = '{"jsonrpc":"2.0","error":{"code":-32700,"message":"Parse error"}}\n'
invalid = '{"jsonrpc":"2.0","error":{"code":-32600,"message":"Invalid Request"}}\n'
result = subprocess.run([args.executable, 'frames'], input=('\n'.join(lines) + '\n').encode(),
                        capture_output=True, timeout=120)
check((result.returncode, result.stdout.decode(), result.stderr.decode()) == (
    1,
    '{"jsonrpc":"2.0","id":1,"method":"tools/list"}\n'
    '{"jsonrpc":"2.0","id":"a","method":"x","params":{}}\n' + parse_error +
    '{"jsonrpc":"2.0","method":"notifications/progress","params":{"progress":1}}\n'
    '{"jsonrpc":"2.0","id":7,"result":{}}\n'
    '{"jsonrpc":"2.0","id":8,"error":{"code":-32602,"message":"Unknown tool: x"}}\n' + invalid + invalid +
    '{"jsonrpc":"2.0","id":9,"error":{"code":-32600,"message":"Invalid Request"}}\n'
    '{"jsonrpc":"2.0","id":10,"error":{"code":-32600,"message":"Invalid Request"}}\n',
    'request 1 tools/list\n'
    'request "a" x (rewritten)\n'
    'invalid, answered with error - -32700 parse error: Parse error\n'
    'notification notifications/progress\n'
    'result 7\n'
    'error 8 -32602 invalid params: Unknown tool: x\n'
    'invalid, answered with error - -32600 invalid request: Invalid Request\n'
    'invalid, answered with error - -32600 invalid request: Invalid Request\n'
    'invalid, answered with error 9 -32600 invalid request: Invalid Request\n'
    'invalid, answered with error 10 -32600 invalid request: Invalid Request\n'), result)
for line in lines[:2] + lines[4:7]:
    message = json.loads(line)
    check(canonical(message) in result.stdout.decode().splitlines(), line)
result = subprocess.run([args.executable, 'frames'], input=b'{"jsonrpc":"2.0","method":"a"}\r\n',
                        capture_output=True, timeout=120)
check((result.returncode, result.stdout, result.stderr) ==
      (0, b'{"jsonrpc":"2.0","method":"a"}\n', b'notification a\n'), result)

# replay: the responses of a recorded session come as the requests finish, in any order.
with tempfile.TemporaryDirectory() as directory:
    path = os.path.join(directory, 'session.jsonl')
    recorded = [request(1, 'server/discover'),
                request(2, 'tools/call', {'name': 'remember', 'arguments': {'text': 'replayed note'}}),
                request(3, 'tools/call', {'name': 'recall', 'arguments': {'query': 'NOTE'}}),
                request(4, 'prompts/get', {'name': 'reflect', 'arguments': {}})]
    with open(path, 'w') as session:
        session.write(json.dumps(recorded[0]) + '\nnot json\n' + json.dumps(recorded[1]) + '\n')
    code, out, err = run(['replay', path])
    answers = sorted((json.loads(line) for line in out.splitlines()), key=lambda message: str(message.get('id')))
    check(code == 0 and err == '' and len(answers) == 3, code, out, err)
    check(answers[0]['result']['supportedVersions'] == [VERSION] and
          answers[1]['result']['structuredContent'] == {'id': 1} and
          answers[2] == {'jsonrpc': '2.0', 'error': {'code': -32700, 'message': 'Parse error'}}, answers)
    with open(path, 'w') as session:
        session.write(''.join(json.dumps(message) + '\n' for message in recorded[1:]))
    code, out, err = run(['replay', path])
    answers = {message['id']: message for message in map(json.loads, out.splitlines())}
    check(code == 0 and err == '' and sorted(answers) == [2, 3, 4], code, out, err)
    check(answers[4]['error']['code'] == -32602 and
          answers[4]['error']['message'] == 'Missing required argument: topic', answers)
    check(answers[3]['result']['structuredContent']['notes'] in ([], [{'id': 1, 'text': 'replayed note'}]),
          answers)
print(f'assistant example checks passed: {checks}')
