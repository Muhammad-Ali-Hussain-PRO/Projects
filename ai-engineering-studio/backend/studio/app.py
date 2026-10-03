"""Bounded local API. Provider calls require server configuration plus bearer auth."""
import asyncio
import importlib
import json
import os
import secrets
import sqlite3
import time
import uuid
from pathlib import Path
from fastapi import FastAPI,Request,HTTPException,WebSocket,WebSocketDisconnect
from fastapi.middleware.cors import CORSMiddleware
from fastapi.responses import StreamingResponse
from starlette.concurrency import run_in_threadpool,iterate_in_threadpool
from . import provider

ROOT=Path(__file__).resolve().parents[2]
PROJECTS={'rag','search','voice','multimodal','review','model','safety','analyst','mcp','chat'}
app=FastAPI(title='Hussain AI Engineering Studio',version='1.0.0')
origins=[origin.strip() for origin in os.environ.get('STUDIO_ALLOWED_ORIGINS','http://localhost:5173,http://127.0.0.1:5173').split(',') if origin.strip()]
_rates={}
MAX_REQUEST_BYTES=24_000_000

def _auth(request):
    token=os.environ.get('STUDIO_API_TOKEN')
    if not token: raise HTTPException(503,'Configure STUDIO_API_TOKEN before connecting the UI to this API.')
    supplied=request.headers.get('authorization','')
    if not secrets.compare_digest(supplied,'Bearer '+token): raise HTTPException(401,'Invalid API token.')

@app.middleware('http')
async def bounds(request:Request,call_next):
    if request.method!='OPTIONS' and request.url.path.startswith('/api/') and request.url.path!='/api/health':
        try:_auth(request)
        except HTTPException as e:
            from fastapi.responses import JSONResponse
            return JSONResponse({'detail':e.detail},status_code=e.status_code)
        key=request.client.host if request.client else 'local'
        now=time.monotonic(); recent=[x for x in _rates.get(key,[]) if now-x<60]
        if len(recent)>=40:
            from fastapi.responses import JSONResponse
            return JSONResponse({'detail':'40 requests/minute limit'},status_code=429)
        _rates[key]=recent+[now]
        if len(_rates)>1000:_rates.clear()
        try: length=int(request.headers.get('content-length','0') or 0)
        except ValueError:
            from fastapi.responses import JSONResponse
            return JSONResponse({'detail':'Invalid Content-Length'},status_code=400)
        if length<0:
            from fastapi.responses import JSONResponse
            return JSONResponse({'detail':'Invalid Content-Length'},status_code=400)
        if length>MAX_REQUEST_BYTES:
            from fastapi.responses import JSONResponse
            return JSONResponse({'detail':'Input too large'},status_code=413)
        if request.method in {'POST','PUT','PATCH'}:
            # Bound the actual body too, including chunked requests with no
            # Content-Length. Replay the bounded bytes to the downstream app.
            chunks=[]; received=0
            async for chunk in request.stream():
                received+=len(chunk)
                if received>MAX_REQUEST_BYTES:
                    from fastapi.responses import JSONResponse
                    return JSONResponse({'detail':'Input too large'},status_code=413)
                chunks.append(chunk)
            request._body=b''.join(chunks)
    return await call_next(request)

# CORS is outside the auth/bounds middleware, including rejected responses.
app.add_middleware(CORSMiddleware,allow_origins=origins,allow_methods=['GET','POST'],allow_headers=['Authorization','Content-Type'])

def _db():
    path=Path(os.environ.get('STUDIO_DB',str(ROOT/'var/studio.sqlite3'))); path.parent.mkdir(parents=True,exist_ok=True)
    db=sqlite3.connect(path)
    db.execute('CREATE TABLE IF NOT EXISTS runs(id TEXT PRIMARY KEY,project TEXT,status TEXT,duration_ms REAL,created REAL)')
    db.execute('CREATE TABLE IF NOT EXISTS feedback(id TEXT PRIMARY KEY,project TEXT,rating INTEGER,comment TEXT,created REAL)')
    return db

@app.get('/api/health')
def health():
    return {'status':'ok','provider_configured':provider.is_configured(),'authentication_configured':bool(os.environ.get('STUDIO_API_TOKEN')),'projects':sorted(PROJECTS)}

@app.post('/api/run/{project}')
async def run(project:str,payload:dict):
    if project not in PROJECTS: raise HTTPException(404,'Unknown project')
    module=importlib.import_module('.'+('mcp_client' if project=='mcp' else project),__package__)
    start=time.perf_counter(); run_id=uuid.uuid4().hex
    try: result=await run_in_threadpool(module.run,payload)
    except provider.ProviderUnavailable as e: raise HTTPException(503,str(e)) from e
    except (ValueError,TypeError,KeyError) as e: raise HTTPException(422,str(e)) from e
    return _record(project,result,start,run_id)

def _record(project,result,start,run_id):
    elapsed=round((time.perf_counter()-start)*1000,3)
    with _db() as db: db.execute('INSERT INTO runs VALUES (?,?,?,?,?)',(run_id,project,result.get('status','ok'),elapsed,time.time()))
    return {**result,'run_id':run_id,'duration_ms':elapsed}

@app.post('/api/stream/{project}')
async def stream(project:str,payload:dict):
    if project not in PROJECTS: raise HTTPException(404,'Unknown project')
    async def events():
        yield 'event: stage\ndata: '+json.dumps({'stage':'running','project':project})+'\n\n'
        try:
            if project=='search':
                from . import search
                iterator=search.iter_events(payload)
                start=time.perf_counter(); run_id=uuid.uuid4().hex
                try:
                    async for event in iterate_in_threadpool(iterator):
                        if event['event']=='done':
                            result=await run_in_threadpool(_record,project,event['data'],start,run_id)
                            yield 'event: result\ndata: '+json.dumps(result)+'\n\n'
                        else:
                            yield 'event: '+event['event']+'\ndata: '+json.dumps(event['data'])+'\n\n'
                finally:
                    await run_in_threadpool(iterator.close)
                return
            result=await run(project,payload)
            # Stream observable stage + completed result. Do not mislabel token streaming.
            yield 'event: result\ndata: '+json.dumps(result)+'\n\n'
        except HTTPException as e: yield 'event: error\ndata: '+json.dumps({'status':e.status_code,'error':e.detail})+'\n\n'
    return StreamingResponse(events(),media_type='text/event-stream')

@app.post('/api/feedback')
def feedback(payload:dict):
    project=payload.get('project'); rating=payload.get('rating'); comment=str(payload.get('comment',''))[:2000]
    if project not in PROJECTS or type(rating)!=int or rating not in range(1,6): raise HTTPException(422,'Valid project and 1–5 rating required')
    id=uuid.uuid4().hex
    with _db() as db:db.execute('INSERT INTO feedback VALUES (?,?,?,?,?)',(id,project,rating,comment,time.time()))
    return {'status':'saved','id':id}

@app.get('/api/metrics')
def metrics():
    with _db() as db:
        return {'runs':[dict(project=p,runs=n,mean_duration_ms=round(d,2)) for p,n,d in db.execute('SELECT project,count(*),avg(duration_ms) FROM runs GROUP BY project')],
                'feedback':[dict(project=p,responses=n,mean_rating=r) for p,n,r in db.execute('SELECT project,count(*),avg(rating) FROM feedback GROUP BY project')],
                'note':'Backend activity only. Runs are not unique people; no real-user count is inferred.'}

@app.websocket('/api/voice')
async def voice_socket(ws:WebSocket):
    # Credentials sent as first message, never in a URL or access log.
    await ws.accept()
    try:
        first=await asyncio.wait_for(ws.receive_json(),10)
        token=os.environ.get('STUDIO_API_TOKEN')
        if not isinstance(first,dict) or not token or not secrets.compare_digest(str(first.get('token','')),token): await ws.close(code=1008); return
        from . import voice
        pending=set()
        async def handle(payload):
            result=await run_in_threadpool(voice.run,payload); await ws.send_json(result)
        while True:
            payload=await ws.receive_json()
            if len(json.dumps(payload))>12_000_000:await ws.close(code=1009);break
            if len(pending)>=4:await ws.send_json({'status':'busy','error':'Four active messages maximum'});continue
            task=asyncio.create_task(handle(payload));pending.add(task);task.add_done_callback(pending.discard)
    except (WebSocketDisconnect,asyncio.TimeoutError):pass
    finally:
        for t in locals().get('pending',set()):t.cancel()
