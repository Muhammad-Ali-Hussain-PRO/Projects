"""Server-only OpenAI REST adapters. No mock answers on configuration failures."""
import os
import json
import base64
import io
import httpx
from jsonschema import validate

MAX_ATTACHMENT_BYTES = 8 * 1024 * 1024
MAX_TEXT_ATTACHMENT_BYTES = 256 * 1024
MAX_IMAGE_PIXELS = 20_000_000
_AUDIO_EXTENSIONS = {
    'audio/webm': 'webm', 'audio/wav': 'wav', 'audio/x-wav': 'wav',
    'audio/wave': 'wav', 'audio/mpeg': 'mp3', 'audio/mp3': 'mp3',
    'audio/mp4': 'm4a', 'audio/ogg': 'ogg', 'application/ogg': 'ogg',
    'audio/flac': 'flac', 'audio/x-flac': 'flac',
}

class ProviderUnavailable(RuntimeError):
    pass

def is_configured():
    return bool(os.environ.get('OPENAI_API_KEY') and os.environ.get('OPENAI_MODEL'))

def _client():
    key = os.environ.get('OPENAI_API_KEY')
    if not key:
        raise ProviderUnavailable('Set OPENAI_API_KEY on your backend; keep it out of the browser.')
    return httpx.Client(base_url='https://api.openai.com/v1/', headers={'Authorization': 'Bearer '+key}, timeout=60)

def _request(path, **kwargs):
    try:
        with _client() as client:
            response = client.post(path, **kwargs)
    except httpx.HTTPError as exc:
        raise ProviderUnavailable('Provider transport failed; check server connectivity and retry.') from exc
    if not response.is_success:
        # Do not echo raw provider errors which can contain submitted private data.
        raise ProviderUnavailable(f'Provider request failed with HTTP {response.status_code}; check your model access and quota.')
    return response

def _generate(content, schema):
    model = os.environ.get('OPENAI_MODEL')
    if not model:
        raise ProviderUnavailable('Set OPENAI_MODEL to a Responses-compatible model available to your account.')
    data = _request('responses', json={'model':model, 'store':False,
        'input':[{'role':'user','content':content}], 'max_output_tokens':2400,
        'text':{'format':{'type':'json_schema','name':'result','strict':True,'schema':schema}}}).json()
    if not isinstance(data,dict) or data.get('status') not in (None,'completed'):
        raise ProviderUnavailable('Provider response did not complete.')
    try:
        chunks = [c.get('text','') for item in data.get('output',[]) for c in item.get('content',[]) if c.get('type')=='output_text']
    except (TypeError,AttributeError) as exc:
        raise ProviderUnavailable('Provider returned an invalid response structure.') from exc
    if not chunks:
        raise ProviderUnavailable('Provider returned no structured text (possibly a refusal).')
    try:
        result = json.loads(''.join(chunks)); validate(result, schema)
        json.dumps(result,allow_nan=False)
    except Exception as exc:
        raise ProviderUnavailable('Provider result failed JSON/schema validation.') from exc
    return result

def generate_json(prompt, schema):
    return _generate([{'type':'input_text','text':str(prompt)}], schema)

def _attachment_bytes(encoded):
    if not isinstance(encoded, str) or not encoded or len(encoded) > 4 * ((MAX_ATTACHMENT_BYTES + 2) // 3):
        raise ProviderUnavailable('Attachment is empty or exceeds 8 MiB.')
    try:
        data = base64.b64decode(encoded, validate=True)
    except (ValueError, TypeError) as exc:
        raise ProviderUnavailable('Attachment is not valid base64.') from exc
    if not data or len(data) > MAX_ATTACHMENT_BYTES:
        raise ProviderUnavailable('Attachment is empty or exceeds 8 MiB.')
    return data

def _image_attachment(encoded, mime):
    data = _attachment_bytes(encoded)
    if mime in ('image/jpg',):
        mime = 'image/jpeg'
    if mime in ('image/bmp', 'image/x-ms-bmp', 'image/gif'):
        # The vision endpoint accepts nonanimated GIF; BMP is converted rather
        # than passed as an unsupported MIME. Check pixels before decoding.
        try:
            from PIL import Image
            with Image.open(io.BytesIO(data)) as image:
                if image.width * image.height > MAX_IMAGE_PIXELS:
                    raise ProviderUnavailable('Image exceeds the 20 million pixel conversion limit.')
                if getattr(image, 'n_frames', 1) != 1:
                    raise ProviderUnavailable('Animated images are not supported in live vision; supply a still frame.')
                if mime != 'image/gif':
                    output = io.BytesIO()
                    image.convert('RGB').save(output, format='PNG')
                    data = output.getvalue()
                    if len(data) > MAX_ATTACHMENT_BYTES:
                        raise ProviderUnavailable('Converted image exceeds 8 MiB.')
                    mime = 'image/png'
        except ProviderUnavailable:
            raise
        except Exception as exc:
            raise ProviderUnavailable('Image could not be prepared for live vision.') from exc
    elif mime not in ('image/png', 'image/jpeg', 'image/webp'):
        raise ProviderUnavailable('Unsupported live vision image MIME type.')
    return 'data:' + mime + ';base64,' + base64.b64encode(data).decode('ascii')

def _attachment_text(value):
    if not isinstance(value, str):
        raise ProviderUnavailable('Text attachment must be a string.')
    try:
        size = len(value.encode('utf-8'))
    except UnicodeError as exc:
        raise ProviderUnavailable('Text attachment contains invalid Unicode.') from exc
    if size > MAX_TEXT_ATTACHMENT_BYTES:
        raise ProviderUnavailable('Text attachment exceeds 256 KiB.')
    return value

def generate_multimodal(prompt, attachments, schema):
    if not isinstance(attachments, list) or not 1 <= len(attachments) <= 8:
        raise ProviderUnavailable('Provide between 1 and 8 attachments.')
    content = [{'type':'input_text','text':str(prompt)}]
    for a in attachments:
        if not isinstance(a, dict):
            raise ProviderUnavailable('Each attachment must be an object.')
        kind = a.get('kind',a.get('type',''))
        mime = a.get('mime',a.get('mime_type','application/octet-stream'))
        if not isinstance(mime, str):
            raise ProviderUnavailable('Attachment MIME type must be a string.')
        mime = mime.split(';', 1)[0].strip().lower()
        encoded = a.get('base64',a.get('data_base64',a.get('content_base64','')))
        # Labels bind content items to the evidence IDs supplied by the caller.
        label = {'attachment_id': str(a.get('id', ''))[:160], 'name': str(a.get('name', ''))[:160], 'type': str(kind)[:40]}
        content.append({'type':'input_text','text':'Attachment metadata: ' + json.dumps(label, ensure_ascii=True)})
        if kind=='image' or mime.startswith('image/'):
            content.append({'type':'input_image','image_url':_image_attachment(encoded, mime)})
        elif kind=='audio' or mime.startswith('audio/'):
            transcript = transcribe_audio(_attachment_bytes(encoded),mime)
            content.append({'type':'input_text','text':'Audio transcript (speech only; sound quality and non-speech content were not analyzed): '+transcript})
        elif encoded:
            try:
                text = _attachment_bytes(encoded).decode('utf-8')
            except UnicodeError as exc:
                raise ProviderUnavailable('Text attachment is not UTF-8.') from exc
            content.append({'type':'input_text','text':_attachment_text(text)})
        else:
            content.append({'type':'input_text','text':_attachment_text(a.get('text',a.get('content','')))})
    return _generate(content,schema)

def transcribe_audio(data, mime):
    if not isinstance(data, bytes) or not data or len(data)>10_000_000:
        raise ProviderUnavailable('Audio must contain between 1 byte and 10 MB.')
    if not isinstance(mime, str):
        raise ProviderUnavailable('Audio MIME type must be a string.')
    mime = mime.split(';',1)[0].strip().lower()
    ext = _AUDIO_EXTENSIONS.get(mime)
    if not ext:
        raise ProviderUnavailable('Unsupported transcription audio MIME type.')
    result = _request('audio/transcriptions',files={'file':('recording.'+ext,data,mime)},data={'model':os.environ.get('OPENAI_STT_MODEL','whisper-1'), 'response_format':'json'}).json()
    text = result.get('text') if isinstance(result,dict) else None
    if not isinstance(text,str) or not text.strip():
        raise ProviderUnavailable('Transcription returned no text.')
    return _attachment_text(text)

def synthesize_speech(text):
    if not isinstance(text,str) or not text.strip() or len(text)>4000:
        raise ProviderUnavailable('Speech input must contain 1 to 4000 characters.')
    audio = _request('audio/speech',json={'model':os.environ.get('OPENAI_TTS_MODEL','tts-1'),'voice':'alloy','input':text,'response_format':'mp3'}).content
    if not audio or len(audio)>MAX_ATTACHMENT_BYTES:
        raise ProviderUnavailable('Speech output is empty or exceeds 8 MiB.')
    return audio
