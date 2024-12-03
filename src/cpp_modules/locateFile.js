Module.locateFile = function (path) {
  if (path.endsWith('.wasm')) {
    return 'assets/stackrabbit/' + path; // Adjust the path as needed
  }
  return path;
};