/** Browser tools perform only the operations identified in their result.mode. */
export type Data=Record<string,any>;
export type Context={model?:Data,modelEvaluation?:Data,portfolio?:Data[],files?:Data[]};
export const CSV_SAMPLE='region,revenue,orders\nNorth,1200,14\nSouth,900,10\nEast,1600,19\nWest,1300,16\nNorth,800,9\nSouth,1100,12\n';
export const SOURCE_URL='https://github.com/Muhammad-Ali-Hussain-PRO/Projects/tree/ai-engineering-studio/ai-engineering-studio';
export const SEARCH_FIXTURES=[
 {id:'S1',title:'Reciprocal rank fusion — original research',url:'https://plg.uwaterloo.ca/~gvcormac/cormacksigir09-rrf.pdf',snippet:'Reciprocal rank fusion combines the ranks assigned by multiple retrieval systems.',origin:'Cached educational excerpt; no live request'},
 {id:'S2',title:'Sentence Transformers — retrieve and rerank',url:'https://www.sbert.net/examples/sentence_transformer/applications/retrieve_rerank/README.html',snippet:'A bi-encoder retrieves candidate passages. A cross-encoder reranks those candidates.',origin:'Cached educational excerpt; no live request'}
];
const stop=new Set('a an and are as at be by do does for from has have how in is it of on or that the this to was what when where which who why with'.split(' '));
const tokens=(text:string)=> (text.toLowerCase().match(/[\p{L}\p{N}_]{2,}/gu)||[]).filter(t=>!stop.has(t));
const text=(value:any,limit=100000)=>String(value??'').slice(0,limit);

export function parseCSV(source:string):string[][] {
 if(source.length>2_000_000) throw new Error('CSV exceeds 2 MB.');
 let rows:string[][]=[],row:string[]=[],cell='',quoted=false,afterQuote=false;
 for(let i=0;i<source.length;i++) {
   const ch=source[i];
   if(quoted) {if(ch==='"'){if(source[i+1]==='"'){cell+='"';i++;}else{quoted=false;afterQuote=true;}}else cell+=ch;continue;}
   if(ch==='"'){if(cell||afterQuote)throw new Error('Invalid quoted CSV field.');quoted=true;continue;}
   if(afterQuote&&ch!==','&&ch!=='\r'&&ch!=='\n')throw new Error('Unexpected text after a quoted field.');
   if(ch===','){row.push(cell);cell='';afterQuote=false;}
   else if(ch==='\n'||ch==='\r'){if(ch==='\r'&&source[i+1]==='\n')i++;row.push(cell);rows.push(row);row=[];cell='';afterQuote=false;}
   else cell+=ch;
 }
 if(quoted)throw new Error('CSV contains an unclosed quoted field.');
 if(cell||row.length||afterQuote){row.push(cell);rows.push(row);}
 if(rows.length<2||rows.length>10001)throw new Error('Provide a header and 1–10,000 data rows.');
 const headers=rows[0];
 if(headers.length>40||headers.some(h=>!h||h.length>100)||new Set(headers).size!==headers.length)throw new Error('Provide 1–40 unique nonempty column names.');
 if(rows.some(r=>r.length!==headers.length))throw new Error('CSV rows have inconsistent column counts.');
 return rows;
}

export function analyzeCSV(source:string,question:string):Data {
 const [headers,...raw]=parseCSV(source);
 const numeric=headers.map((_,i)=>raw.some(r=>r[i]!=='')&&raw.every(r=>r[i]===''||Number.isFinite(Number(r[i]))));
 const q=question.toLowerCase();
 if(/\b(select|delete|update|insert|drop|with|pragma|attach)\b/i.test(q))throw new Error('Browser analytics accepts a limited natural-language aggregate, not SQL. Use the backend for restricted SELECT queries.');
 const column=headers.findIndex((h,i)=>numeric[i]&&q.includes(h.toLowerCase()));
 const index=column>=0?column:numeric.indexOf(true);
 const group=headers.findIndex((h,i)=>!numeric[i]&&(q.includes('by '+h.toLowerCase())||q.includes('per '+h.toLowerCase())));
 const op=/\b(count|how many|number of)\b/.test(q)?'count':/\b(average|mean|avg)\b/.test(q)?'average':/\b(total|sum)\b/.test(q)?'sum':/\b(highest|maximum|max)\b/.test(q)?'max':/\b(lowest|minimum|min)\b/.test(q)?'min':/\b(show|list|preview)\b/.test(q)?'preview':null;
 if(!op)throw new Error('Use count, total, average, minimum, maximum, or preview, and name a column.');
 if(op!=='count'&&op!=='preview'&&index<0)throw new Error('This operation needs a numeric column.');
 const schema=headers.map((name,i)=>({name,type:numeric[i]?'numeric':'text'}));
 if(op==='preview')return {status:'ok',mode:'browser_local_grammar',columns:headers,rows:raw.slice(0,20),source_rows:raw.length,schema,plan:'Parse validated CSV → return first 20 rows',executed:'Browser array operations; no SQL execution'};
 const groups=new Map<string,string[][]>();
 for(const row of raw){const key=group>=0?row[group]:'All rows';if(!groups.has(key))groups.set(key,[]);groups.get(key)!.push(row);}
 const rows=[...groups].map(([key,records])=>{
   const values=records.filter(r=>r[index]!=='').map(r=>Number(r[index]));
   const value=op==='count'?records.length:values.length===0?null:op==='sum'?values.reduce((a,b)=>a+b,0):op==='average'?values.reduce((a,b)=>a+b,0)/values.length:op==='max'?Math.max(...values):Math.min(...values);
   return group>=0?[key,value]:[value];
 }).sort((a,b)=>Number(b[b.length-1]??0)-Number(a[a.length-1]??0)).slice(0,200);
 return {status:'ok',mode:'browser_local_grammar',columns:group>=0?[headers[group],`${op}_${op==='count'?'rows':headers[index]}`]:[`${op}_${op==='count'?'rows':headers[index]}`],rows,source_rows:raw.length,schema,plan:`Parse validated CSV → ${op}${op==='count'?' rows':' '+headers[index]}${group>=0?' grouped by '+headers[group]:''}`,executed:'Browser array operations; no SQL execution',chart:group>=0?{labels:rows.map(r=>r[0]),values:rows.map(r=>r[1])}:null};
}

export function retrieveText(documents:Data[],query:string):Data {
 if(!query.trim())throw new Error('Enter a question.');
 const terms=tokens(query),pages:Data[]=[];let pageCount=0;
 for(const [d,doc] of documents.entries()){
   const source=Array.isArray(doc.pages)?doc.pages:text(doc.text).split('\f').map((t,i)=>({page:i+1,text:t}));
   for(const [i,page] of source.entries()){
     pageCount++;const original=text(page.text),words=[...original.matchAll(/\S+/g)];
     for(let start=0;start<words.length;start+=150){
       const end=Math.min(start+180,words.length),last=words[end-1];
       const content=original.slice(words[start].index,last.index+last[0].length),counts=new Map<string,number>();
       for(const t of tokens(content))counts.set(t,(counts.get(t)||0)+1);
       pages.push({document_id:doc.id||`document-${d+1}`,document_name:doc.name||'Text corpus',page:page.page||i+1,text:content,citation_id:`D${d+1}-P${page.page||i+1}-C${Math.floor(start/150)+1}`,counts,length:[...counts.values()].reduce((a,b)=>a+b,0)});
       if(start+180>=words.length)break;
     }
   }
 }
 if(pages.length>5000)throw new Error('Corpus exceeds the browser chunk limit.');
 const avg=pages.reduce((a,p)=>a+p.length,0)/Math.max(1,pages.length),documentFrequency=new Map<string,number>();
 for(const page of pages)for(const term of page.counts.keys())documentFrequency.set(term,(documentFrequency.get(term)||0)+1);
 const hits=pages.map(p=>{
   let score=0;
   for(const term of new Set(terms)){const f=p.counts.get(term)||0;const df=documentFrequency.get(term)||0;if(f)score+=Math.log(1+(pages.length-df+.5)/(df+.5))*f*2.5/(f+1.5*(.25+.75*p.length/Math.max(avg,1)));}
   const {counts,length,...hit}=p;return {...hit,score};
 }).filter(h=>h.score>0).sort((a,b)=>b.score-a.score).slice(0,4);
 const citations=hits.slice(0,3).map(h=>({citation_id:h.citation_id,document_id:h.document_id,document_name:h.document_name,page:h.page,quote:h.text.slice(0,650)}));
 return {status:'ok',mode:'browser_local_BM25',answer_mode:'extractive',supported:!!citations.length,answer:citations.length?citations.map(c=>`"${c.quote}" [${c.citation_id}]`).join('\n\n'):'No supporting passage was found in the supplied text.',citations,hits,stats:{pages:pageCount,chunks:pages.length},notes:['Browser mode retrieves text with BM25. PDF extraction, vector fusion, reranking, and labeled evaluation are available in the backend.']};
}

export function inferModel(request:string,model:Data,evaluation?:Data):Data {
 if(!model?.vocabulary||!Array.isArray(model.coef))throw new Error('Trained model artifact is unavailable. Reload or connect the backend.');
 const words=request.toLowerCase().match(/[\p{L}\p{N}_]{2,}/gu)||[],features=[...words,...words.slice(0,-1).map((w,i)=>w+' '+words[i+1])],counts=new Map<string,number>(),vector=new Float64Array(model.idf.length);
 for(const feature of features)counts.set(feature,(counts.get(feature)||0)+1);
 let matched=0;
 for(const [term,count] of counts){const idx=model.vocabulary[term];if(Number.isInteger(idx)){vector[idx]=(1+Math.log(count))*model.idf[idx];matched++;}}
 const norm=Math.sqrt(vector.reduce((sum,v)=>sum+v*v,0));if(norm)for(let i=0;i<vector.length;i++)vector[i]/=norm;
 const raw=model.coef.map((row:number[],i:number)=>{const score=row.reduce((sum,v,j)=>sum+v*vector[j],model.intercept[i]);return 1/(1+Math.exp(-Math.max(-700,Math.min(700,score))));});
 const sum=raw.reduce((a:number,b:number)=>a+b,0),ranking=model.classes.map((intent:string,i:number)=>({intent,probability:raw[i]/sum})).sort((a:Data,b:Data)=>b.probability-a.probability);
 const intent=matched?ranking[0].intent:null;
 return {status:!matched||ranking[0].probability<.4?'needs_review':'ok',mode:'browser_trained_classifier',request,model:'SQL intent domain adaptation baseline',model_type:'TF-IDF + SGD logistic classifier',training_status:'trained_and_evaluated',causal_lm_status:'unrun_optional_entrypoint',prediction:{intent:ranking[0].intent,confidence:ranking[0].probability,matched_features:matched,ranking},intent,sql_template:intent?model.sql_templates[intent]:null,executed:false,review_required:true,notes:[...(model.limitations||[]),'Confidence is uncalibrated. Fixed SQL templates need validated parameter binding. No SQL is executed.'],evaluation:evaluation?{split_counts:evaluation.split_counts,test_baselines:Object.fromEntries(Object.entries(evaluation.metrics||{}).map(([key,value]:[string,any])=>[key,value.test])),dataset_sha256:evaluation.dataset_sha256}:undefined};
}

export function reviewCode(code:string):Data {
 if(!code.trim())throw new Error('Paste code to inspect.');
 const findings:Data[]=[];
 const rules=[['security','high',/\b(eval|exec)\s*\(/,'Dynamic execution requires trusted input and a documented need.'],['security','high',/(?:shell\s*=\s*True|os\.system\s*\()/,'Shell execution can turn untrusted input into commands.'],['security','high',/(?:api[_-]?key|password|secret)\s*=\s*['"][^'"]{6,}['"]/i,'A possible hardcoded secret is present.'],['correctness','medium',/def\s+\w+\([^\n]*=\s*(?:\[\]|\{\})/,'Mutable defaults share state across calls.'],['correctness','medium',/except\s*:/,'A bare exception handler can conceal unrelated failures.'],['tests','medium',/\bprint\s*\(/,'Consider assertions or structured logging for production behavior.']];
 code.split('\n').forEach((line,i)=>rules.forEach(([role,severity,pattern,message])=>{if((pattern as RegExp).test(line))findings.push({role,severity,line:i+1,message,evidence:line.trim()});}));
 return {status:'ok',mode:'browser_static_heuristics',summary:findings.length?`${findings.length} pattern findings need review.`:'No configured pattern matched.',findings,roles:[{role:'Reviewer',scope:'Line-based correctness patterns'},{role:'Security',scope:'Dynamic execution and possible secrets'},{role:'Test writer',scope:'Suggested boundary and invalid-input cases'}],test_suggestions:['Check a representative valid input with an explicit expected output.','Check boundaries and invalid inputs allowed by the contract.'],notes:['These browser checks are heuristics, not Python AST parsing or a security guarantee. Backend mode provides AST inspection and optional model proposals. Submitted code is never executed.']};
}

export function safetyScan(source:string):Data {
 const rules=[['instruction_override',/\b(ignore|disregard|forget)\b.{0,45}\b(previous|prior|above|system|instructions|rules)\b/is],['prompt_exfiltration',/\b(reveal|print|expose|show|leak)\b.{0,60}\b(system prompt|hidden instructions|canary|secret token)\b/is],['role_spoof',/(\[system\]|<\|system\|>|developer message:|system override:)/i],['tool_redirect',/\b(send|upload|post|exfiltrate)\b.{0,70}\b(credentials|api keys|private data|secret|canary)\b/is]];
 const evidence=rules.flatMap(([rule,pattern])=>{const match=(pattern as RegExp).exec(source);return match?[{rule,match:match[0],start:match.index,end:match.index+match[0].length}]:[];});
 const spans:Data[]=[];
 for(const [type,pattern] of [['email',/\b[A-Za-z0-9._%+-]+@[A-Za-z0-9.-]+\.[A-Za-z]{2,}\b/g],['ssn',/\b\d{3}-\d{2}-\d{4}\b/g],['api_key',/\bsk-(?:test-)?[A-Za-z0-9_-]{12,}\b/g]] as [string,RegExp][]){for(const m of source.matchAll(pattern))spans.push({type,start:m.index,end:m.index+m[0].length});}
 let redacted=source;for(const span of [...spans].sort((a,b)=>b.start-a.start))redacted=redacted.slice(0,span.start)+`[REDACTED_${span.type.toUpperCase()}]`+redacted.slice(span.end);
 return {status:'ok',mode:'browser_heuristics',guard:{decision:evidence.length?'block':'allow',blocked:!!evidence.length,evidence},redaction:{redacted,spans},notes:['Pattern matching has false positives and false negatives. This scan does not certify model safety. Backend tools include a labeled synthetic corpus and optional configured-provider probes.']};
}

export function runOffline(id:string,payload:Data,context:Context={}):Data {
 if(id==='rag')return retrieveText(payload.documents||[{id:'corpus',name:'Browser text corpus',text:payload.text}],text(payload.query));
 if(id==='chat')return {...retrieveText(context.portfolio||[],text(payload.query)),scope:'Versioned candidate facts only. No external-user count is inferred.'};
 if(id==='review')return reviewCode(text(payload.code));
 if(id==='analyst')return analyzeCSV(text(payload.csv||CSV_SAMPLE,2_000_001),text(payload.question));
 if(id==='model')return inferModel(text(payload.request),context.model||{},context.modelEvaluation);
 if(id==='safety')return safetyScan(text(payload.text));
 if(id==='multimodal')return {status:'ok',mode:'browser_metadata',summary:`Inspected ${context.files?.length||0} selected files and a text input.`,evidence_manifest:[...(context.files||[]),...(payload.text?[{name:'Pasted text',type:'text',characters:payload.text.length,lines:payload.text.split('\n').length}]:[])],notes:['File SHA-256, image dimensions, and browser-readable audio duration are measured locally. Metadata alone does not interpret images or transcribe audio. Text/code is never executed.']};
 if(id==='search'){
  const terms=tokens(text(payload.query)),sources=SEARCH_FIXTURES.filter(s=>terms.some(t=>tokens(s.snippet+' hybrid retrieval rerank search').includes(t)));
  return {status:sources.length?'ok':'empty',mode:'cached_fixture',query:payload.query,plan:[payload.query,`${payload.query} primary sources`],answer:sources.length?sources.map(s=>`${s.snippet} [${s.id}]`).join('\n\n'):'The educational cached fixture has no evidence for this question. Connect a configured search backend for live research.',sources,citations:sources.map(s=>({source_id:s.id,url:s.url,quote:s.snippet})),notes:['This is cached educational evidence. No network search, crawling, or live model call occurred. Backend mode uses the configured search-provider API.']};
 }
 if(id==='mcp')return {status:'requires_backend',mode:'browser_protocol_explanation',summary:'Connect the backend to initialize a real stdio MCP session.',tools:[{name:'list_projects',purpose:'Read the ten-project catalog'},{name:'read_context',purpose:'Read allowlisted candidate context'},{name:'query_sample',purpose:'Run bounded read-only sample analytics'}],executed:false,notes:['The browser cannot spawn the Python MCP server. Backend mode uses the MCP SDK for initialization, discovery, and a real tool invocation.']};
 if(id==='voice')return {status:'requires_browser_permission',mode:'browser_audio',summary:'Use the microphone and speech playback controls in this workspace.',notes:['Browser RMS silence detection and browser text-to-speech are separate capabilities. A provider conversation requires the configured backend.']};
 throw new Error('Unknown project.');
}
