import {cp,mkdir} from 'node:fs/promises';
import {fileURLToPath} from 'node:url';
const root=fileURLToPath(new URL('../../',import.meta.url));
const target=fileURLToPath(new URL('../public/',import.meta.url));
await mkdir(target,{recursive:true});
for(const [from,to] of [['data/catalog.json','data/catalog.json'],['data/portfolio.json','data/portfolio.json'],['artifacts/model/adapted_model.json','data/model.json'],['artifacts/model/evaluation.json','data/model-evaluation.json'],['artifacts/safety/local_report.json','data/safety-evaluation.json'],['artifacts/verification/backend_tests.json','data/backend-tests.json'],['docs','docs']]) {
  await mkdir(new URL('../public/'+to.split('/').slice(0,-1).join('/')+'/',import.meta.url),{recursive:true});
  await cp(root+from,target+to,{recursive:true});
}
