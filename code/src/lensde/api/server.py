"""Standard-library workbench server; binds only to localhost."""
from __future__ import annotations
import argparse
import json
import mimetypes
import os
import uuid
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
from urllib.parse import parse_qs,urlparse

from lensde.config import BenchmarkCatalog
from lensde.engine import core
from . import service


def create_server(port=8767):
    jobs=service.JobManager()
    jobs.prepare_trace_builds()
    class Handler(BaseHTTPRequestHandler):
        server_version='LensDE/0.1'
        def send(self,status,body,content_type='application/json; charset=utf-8'):
            self.send_response(status);self.send_header('Content-Type',content_type)
            self.send_header('Content-Length',str(len(body)));self.send_header('Cache-Control','no-store')
            self.end_headers();self.wfile.write(body)
        def json(self,status,value):self.send(status,service.strict_json(value).encode('utf-8'))
        def body(self):
            size=int(self.headers.get('Content-Length','0'))
            if size>20_000_000:raise ValueError('Project payload too large')
            return json.loads(self.rfile.read(size).decode('utf-8')) if size else {}
        def file(self,path):
            content=mimetypes.guess_type(str(path))[0] or 'application/octet-stream'
            if path.suffix in {'.js','.html','.css','.json','.csv','.txt'}:content+='; charset=utf-8'
            self.send(200,path.read_bytes(),content)
        def do_GET(self):
            try:
                url=urlparse(self.path);query=parse_qs(url.query)
                get=lambda key,default='':query.get(key,[default])[0]
                if url.path=='/api/health':self.json(200,{'service':'LensDE','project_root':str(service.PROJECT),'pid':os.getpid()})
                elif url.path=='/api/benchmarks':self.json(200,{'benchmarks':[x.as_dict() for x in BenchmarkCatalog().list_benchmarks()]})
                elif url.path=='/api/benchmark':self.json(200,service.benchmark(get('id','S3')))
                elif url.path=='/api/default':
                    prepared=service.benchmark('S3');prepared['metadata']['default_template']='S3'
                    self.json(200,prepared)
                elif url.path=='/api/jobs':self.json(200,jobs.status(get('id')))
                elif url.path=='/api/glass-libraries':self.json(200,{'libraries':core.list_glass_libraries()})
                elif url.path=='/api/nearest-glass':
                    self.json(200,{'glass':core.nearest_glass(get('library'),float(get('nd','1.5168')),float(get('vd','64.17')))})
                elif url.path in {'/api/materials','/api/glasses'}:
                    records=[dict(row) for row in core.list_glasses(get('q'),get('libraries'),int(get('limit','250')))]
                    for record in records:
                        record['mode']='glass'
                        record['material']={key:record[key] for key in ('name','reference','nd','vd','filename','source_path') if key in record}
                        record['material']['mode']='glass'
                    self.json(200,{'materials':records,'glasses':records,'libraries':core.list_glass_libraries()})
                elif url.path=='/api/output':self.file(jobs.output(get('job_id'),get('file')))
                elif url.path=='/api/download':
                    root=(service.PROJECT/'exports').resolve();path=(root/get('id')).resolve()
                    if not path.is_relative_to(root) or not path.is_file():raise FileNotFoundError('Missing export')
                    self.file(path)
                else:
                    root=(service.PROJECT/'web').resolve();path=(root/(url.path.lstrip('/') or 'index.html')).resolve()
                    if not path.is_relative_to(root) or not path.is_file():raise FileNotFoundError('Missing web file')
                    self.file(path)
            except KeyError as error:self.json(404,{'error':str(error)})
            except FileNotFoundError as error:self.json(404,{'error':str(error)})
            except Exception as error:self.json(400,{'error':str(error)})
        def do_POST(self):
            try:
                payload=self.body();path=urlparse(self.path).path
                if path=='/api/normalize':self.json(200,{'project':service.normalize(payload['project'])})
                elif path=='/api/import-project':self.json(200,service.import_project(payload['document'],payload.get('project')))
                elif path=='/api/merit-preview':self.json(200,service.merit_preview(payload['project']))
                elif path=='/api/jobs':self.json(202,jobs.start(payload))
                elif path=='/api/set-vignetting':self.json(202,jobs.start({'project':payload['project'],'kind':'vignetting'}))
                elif path=='/api/stop':self.json(200,jobs.stop(payload['job_id']))
                elif path=='/api/resume':self.json(202,jobs.resume(payload['job_id'],payload.get('project')))
                elif path=='/api/export':
                    project=service.normalize(payload['project']);format=payload.get('format','json')
                    if format not in {'json','system','zmx','seq'}:raise ValueError('Unknown export format')
                    root=service.PROJECT/'exports';root.mkdir(exist_ok=True)
                    name=uuid.uuid4().hex+'.'+('json' if format=='system' else format);target=root/name
                    if format=='json':target.write_text(service.strict_json(project)+'\n',encoding='utf-8')
                    elif format=='system':target.write_text(service.strict_json(core.system_with_ray_aiming(project['system'],project.get('options',{})))+'\n',encoding='utf-8')
                    elif format=='zmx':core.export_zemax_zmx(project,target)
                    else:core.export_codev_seq(project,target)
                    self.json(200,{'url':'/api/download?id='+name,'filename':name})
                else:self.json(404,{'error':'Unknown API route'})
            except Exception as error:self.json(400,{'error':str(error)})
        def log_message(self,*args):pass
    class LocalServer(ThreadingHTTPServer):
        allow_reuse_address=False
    return LocalServer(('127.0.0.1',port),Handler)


def main(argv=None):
    parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('--port',type=int,default=8767)
    args=parser.parse_args(argv);server=create_server(args.port)
    print(f'LensDE workbench: http://127.0.0.1:{server.server_port}',flush=True)
    try:server.serve_forever()
    finally:server.server_close()


if __name__=='__main__':main()
