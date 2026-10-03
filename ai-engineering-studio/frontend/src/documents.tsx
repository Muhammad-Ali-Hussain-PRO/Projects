import React from 'react';

function inline(text:string):React.ReactNode[]{
 const out:React.ReactNode[]=[];let start=0;
 const expression=/\[([^\]]+)\]\(([^)]+)\)|`([^`]+)`|\*\*([^*]+)\*\*/g;
 for(const match of text.matchAll(expression)){
   const index=match.index!;if(index>start)out.push(text.slice(start,index));
   if(match[1]){let target='#';try{const parsed=new URL(match[2]);if(['https:','http:'].includes(parsed.protocol))target=parsed.href;}catch{}out.push(<a key={index} href={target} target="_blank" rel="noopener noreferrer">{match[1]}</a>);}
   else if(match[3])out.push(<code key={index}>{match[3]}</code>);
   else out.push(<strong key={index}>{match[4]}</strong>);
   start=index+match[0].length;
 }
 if(start<text.length)out.push(text.slice(start));return out;
}

/** Deliberately small Markdown renderer: every node is React text, never HTML. */
export function MarkdownDocument({source}:{source:string}){
 const lines=source.split('\n'),blocks:React.ReactNode[]=[];let i=0;
 while(i<lines.length){
   const line=lines[i];if(!line.trim()){i++;continue;}
   if(line.startsWith('```')){const language=line.slice(3).trim(),code:string[]=[];i++;while(i<lines.length&&!lines[i].startsWith('```'))code.push(lines[i++]);i++;blocks.push(<div className="doc-code" key={i}>{language&&<span>{language}</span>}<pre><code>{code.join('\n')}</code></pre></div>);continue;}
   const heading=/^(#{1,6})\s+(.+)$/.exec(line);if(heading){const tag=('h'+Math.min(heading[1].length,4)) as any;blocks.push(React.createElement(tag,{key:i},inline(heading[2])));i++;continue;}
   if(/^\s*[-*]\s+/.test(line)){const entries:string[]=[];while(i<lines.length&&/^\s*[-*]\s+/.test(lines[i]))entries.push(lines[i++].replace(/^\s*[-*]\s+/,''));blocks.push(<ul key={i}>{entries.map((entry,j)=><li key={j}>{inline(entry)}</li>)}</ul>);continue;}
   if(/^\s*\d+\.\s+/.test(line)){const entries:string[]=[];while(i<lines.length&&/^\s*\d+\.\s+/.test(lines[i]))entries.push(lines[i++].replace(/^\s*\d+\.\s+/,''));blocks.push(<ol key={i}>{entries.map((entry,j)=><li key={j}>{inline(entry)}</li>)}</ol>);continue;}
   if(line.startsWith('|')&&lines[i+1]?.match(/^\|?\s*:?-{3,}/)){const cells=(s:string)=>s.split('|').slice(1,-1).map(c=>c.trim());const headings=cells(line),rows:string[][]=[];i+=2;while(i<lines.length&&lines[i].startsWith('|'))rows.push(cells(lines[i++]));blocks.push(<div className="table-scroll" key={i}><table><thead><tr>{headings.map((h,j)=><th key={j}>{inline(h)}</th>)}</tr></thead><tbody>{rows.map((row,r)=><tr key={r}>{row.map((cell,c)=><td key={c}>{inline(cell)}</td>)}</tr>)}</tbody></table></div>);continue;}
   const paragraph:string[]=[line];i++;while(i<lines.length&&lines[i].trim()&&!/^(#{1,6}\s|```|[-*]\s|\d+\.\s)/.test(lines[i]))paragraph.push(lines[i++]);blocks.push(<p key={i}>{inline(paragraph.join(' '))}</p>);
 }
 return <article className="markdown-document">{blocks}</article>;
}
