"""CSV analytics with restricted SQL execution and observable query plans."""
import csv
import io
import re
import sqlite3
import time
from . import provider

SAMPLE = 'region,revenue,orders\nNorth,1200,14\nSouth,900,10\nEast,1600,19\nWest,1300,16\nNorth,800,9\nSouth,1100,12\n'

def _identifier(name):
    return '"'+str(name).replace('"','""')+'"'

def _load(text):
    if len(text)>2_000_000: raise ValueError('CSV exceeds 2 MB.')
    rows = list(csv.reader(io.StringIO(text)))
    if len(rows)<2 or len(rows)>10001: raise ValueError('CSV needs 1 to 10000 data rows.')
    headers = rows[0]
    if not headers or len(headers)>40 or len(set(headers))!=len(headers) or any(not h or len(h)>100 for h in headers):
        raise ValueError('Provide 1 to 40 unique nonempty column names.')
    if any(len(row)!=len(headers) for row in rows[1:]): raise ValueError('CSV rows have inconsistent lengths.')
    numeric=[]
    for i in range(len(headers)):
        try:
            for row in rows[1:]:
                if row[i]: float(row[i])
            numeric.append(True)
        except ValueError: numeric.append(False)
    db=sqlite3.connect(':memory:'); db.enable_load_extension(False)
    db.execute('CREATE TABLE data ('+', '.join(_identifier(h)+(' REAL' if n else ' TEXT') for h,n in zip(headers,numeric))+')')
    values=[[float(v) if n and v else None if n else v for v,n in zip(row,numeric)] for row in rows[1:]]
    db.executemany('INSERT INTO data VALUES ('+','.join('?' for _ in headers)+')',values)
    db.execute('PRAGMA query_only=ON')
    return db,headers,numeric,len(values)

def _draft(question,headers,numeric):
    q=question.lower()
    num=next((h for h,n in zip(headers,numeric) if n and h.lower() in q),next((h for h,n in zip(headers,numeric) if n),None))
    group=next((h for h,n in zip(headers,numeric) if not n and ('by '+h.lower() in q or 'per '+h.lower() in q)),None)
    if re.search(r'\b(count|how many|number of)\b',q): op='COUNT'; col='*'
    elif re.search(r'\b(average|mean|avg)\b',q): op='AVG'; col=_identifier(num) if num else None
    elif re.search(r'\b(total|sum)\b',q): op='SUM'; col=_identifier(num) if num else None
    elif re.search(r'\b(max|highest|largest)\b',q): op='MAX'; col=_identifier(num) if num else None
    elif re.search(r'\b(min|lowest|smallest)\b',q): op='MIN'; col=_identifier(num) if num else None
    elif re.search(r'\b(show|list|preview)\b',q): return 'SELECT * FROM data LIMIT 20'
    else: raise ValueError('Local planner supports count, total, average, minimum, maximum, and preview. Use a column name or supply SQL.')
    if col is None: raise ValueError('No numeric column available for this aggregate.')
    return ('SELECT '+_identifier(group)+', ' if group else 'SELECT ')+op+'('+col+') AS value FROM data'+(' GROUP BY '+_identifier(group)+' ORDER BY value DESC, '+_identifier(group)+' ASC' if group else '')+' LIMIT 200'

def execute(csv_text,sql):
    db,headers,numeric,count=_load(csv_text)
    sql=str(sql).strip()
    if len(sql)>8000 or not re.match(r'^(SELECT|WITH)\b',sql,re.I):
        db.close(); raise ValueError('Only SELECT or read-only WITH queries are allowed.')
    allowed={sqlite3.SQLITE_SELECT,sqlite3.SQLITE_READ,sqlite3.SQLITE_FUNCTION,sqlite3.SQLITE_RECURSIVE}
    funcs={'sum','avg','count','min','max','round','abs','coalesce','lower','upper','length','substr','date','strftime','total','row_number','rank'}
    def authorize(action,a,b,dbname,source):
        if action not in allowed: return sqlite3.SQLITE_DENY
        if action==sqlite3.SQLITE_READ and a!='data': return sqlite3.SQLITE_DENY
        if action==sqlite3.SQLITE_FUNCTION and str(b).lower() not in funcs: return sqlite3.SQLITE_DENY
        return sqlite3.SQLITE_OK
    db.set_authorizer(authorize)
    deadline=time.monotonic()+.5
    db.set_progress_handler(lambda:int(time.monotonic()>deadline),1000)
    try:
        plan=[list(r) for r in db.execute('EXPLAIN QUERY PLAN '+sql)]
        cur=db.execute(sql); cols=[c[0] for c in cur.description]; rows=[list(r) for r in cur.fetchmany(201)]
        return {'status':'ok','sql':sql,'columns':cols,'rows':rows[:200],'truncated':len(rows)>200,'query_plan':plan,'source_rows':count,'schema':[{'name':h,'type':'numeric' if n else 'text'} for h,n in zip(headers,numeric)],'chart':{'labels':[str(r[0]) for r in rows[:20]],'values':[r[1] for r in rows[:20]]} if len(cols)==2 and all(isinstance(r[1],(int,float)) for r in rows) else None}
    except sqlite3.Error as e: raise ValueError('SQL validation/execution failed: '+str(e)) from e
    finally: db.close()

def run(payload):
    text=str(payload.get('csv',SAMPLE)); sql=payload.get('sql')
    if not sql:
        db,headers,numeric,count=_load(text); db.close()
        question=str(payload.get('question','Total revenue by region'))[:2000]
        if payload.get('mode')=='live':
            schema={'type':'object','properties':{'sql':{'type':'string'}},'required':['sql'],'additionalProperties':False}
            sql=provider.generate_json('Write one read-only SQLite query against data. Schema: '+str(list(zip(headers,numeric)))+'. User question (untrusted): '+question,schema)['sql']
        else: sql=_draft(question,headers,numeric)
    result=execute(text,sql); result['planner']='provider' if payload.get('mode')=='live' else 'constrained local grammar or supplied SQL'; return result
