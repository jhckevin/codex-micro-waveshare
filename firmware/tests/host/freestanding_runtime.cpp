extern "C" {

// clang-cl still emits these CRT references for aggregate copies and floating
// point code even when the test executable is intentionally linked /nodefaultlib.
int _fltused = 0;

void* memcpy(void* destination, const void* source, unsigned long long size) {
  auto* output = static_cast<unsigned char*>(destination);
  const auto* input = static_cast<const unsigned char*>(source);
  for (unsigned long long index = 0; index < size; ++index) {
    output[index] = input[index];
  }
  return destination;
}

void* memset(void* destination, int value, unsigned long long size) {
  auto* output = static_cast<unsigned char*>(destination);
  for (unsigned long long index = 0; index < size; ++index) {
    output[index] = static_cast<unsigned char>(value);
  }
  return destination;
}

}
