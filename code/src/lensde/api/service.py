"""Thin HTTP-facing adapters for shared application services."""
from lensde.engine import core,state
from lensde.projects import localize,normalize,benchmark
from lensde.runtime.jobs import JobManager,strict_json
PROJECT=state.PROJECT_ROOT

def merit_preview(project):
    prepared=normalize(project)
    return {'merit':prepared['merit'],'effective_merit':core.auto_merit_config(prepared),
            'auto_merit':bool(prepared['merit'].get('auto_merit',False))}


def import_project(document,project=None):
    if not isinstance(document,dict):raise ValueError('Project JSON must be an object')
    if isinstance(document.get('project'),dict):document=document['project']
    if isinstance(document.get('system'),dict):
        prepared=normalize(document)
        kind='project'
    elif isinstance(document.get('surfaces'),list):
        prepared=normalize(project) if project is not None else benchmark('S3')['project']
        prepared['system']=document
        prepared['variables']=[]
        prepared=normalize(prepared)
        kind='system'
    else:raise ValueError('Expected a project with system or a native system with surfaces')
    return {'project':prepared,'metadata':{'input_type':kind,'variables_cleared':kind=='system'}}
