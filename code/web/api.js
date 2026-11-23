export class ServiceError extends Error {
  constructor(message, status, details) {
    super(message); this.status = status; this.details = details;
  }
}
export async function request(path, body, {signal} = {}) {
  const response = await fetch(path, {
    method: body === undefined ? "GET" : "POST",
    headers: body === undefined ? {"Accept":"application/json"} : {"Accept":"application/json", "Content-Type":"application/json"},
    body: body === undefined ? undefined : JSON.stringify(body), signal,
  });
  let data;
  try { data = await response.json(); }
  catch { throw new ServiceError(`HTTP ${response.status}: invalid JSON response`, response.status); }
  if (!response.ok || data?.error) {
    const detail = data?.error ?? data?.message ?? `HTTP ${response.status}`;
    throw new ServiceError(typeof detail === "string" ? detail : (detail.message ?? JSON.stringify(detail)), response.status, data);
  }
  return data;
}
export const api = {
  benchmarks: () => request("/api/benchmarks"),
  experiments: () => request("/api/experiments"),
  benchmark: id => request(`/api/benchmark?id=${encodeURIComponent(id)}`),
  defaultProject: () => request("/api/default"),
  normalize: project => request("/api/normalize", {project}),
  preset: body => request("/api/preset", body),
  start: payload => request("/api/jobs", payload),
  job: id => request(`/api/jobs?id=${encodeURIComponent(id)}`),
  stop: job_id => request("/api/stop", {job_id}),
  resume: (job_id, project) => request("/api/resume", {job_id, project}),
  materials: (q, libraries) => request(`/api/materials?q=${encodeURIComponent(q)}&libraries=${encodeURIComponent(libraries)}&limit=5000`),
  glassLibraries: () => request('/api/glass-libraries'),
  meritPreview: project => request('/api/merit-preview', {project}),
  setVignetting: project => request('/api/set-vignetting', {project}),
  importProject: (document,project) => request('/api/import-project', {document,project}),
  export: (project, format) => request("/api/export", {project, format}),
};
