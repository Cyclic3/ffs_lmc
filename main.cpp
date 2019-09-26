#include <iostream>
#include <iomanip>
#include <memory>
#include <optional>
#include <stdexcept>
#include <vector>

#include <cstring>
#include <cstdint>

#include <sys/mman.h>

class exec_buf {
private:
  uint8_t* buf = nullptr;
  size_t buf_len = 0;
  size_t len = 0;

public:
  void reset() { buf = nullptr; buf_len = 0; len = 0; ::munmap(buf, buf_len); }

  void reserve(size_t new_len) {
    if (new_len <= buf_len) {
      len = new_len;
      return;
    }

    uint8_t* old_buf = buf;
    size_t old_buf_len = buf_len;
    size_t old_len = len;

    if ((buf = reinterpret_cast<uint8_t*>(::mmap(nullptr, new_len, PROT_EXEC|PROT_READ|PROT_WRITE, MAP_ANONYMOUS|MAP_PRIVATE, -1, 0))) == MAP_FAILED) {
      int a = errno;
      throw std::runtime_error("Could not alloc memory");
    }
    buf_len = new_len;
    ::memcpy(buf, old_buf, old_len);
    if (old_buf)
      ::munmap(old_buf, old_buf_len);
  }

  void resize(size_t new_len) {
    reserve(new_len);
    len = new_len;
  }

  void push_back(uint8_t i) {
    resize(len + 1);
    buf[len - 1] = i;
  }

  /// XXX: So bad...
  template<typename T>
  void hack_back(const T& t) {
    size_t offset = len;
    resize(len + sizeof(T));
    memcpy(buf + offset, &t, sizeof(T));
  }

  /// XXX: So bad...
  void append(std::initializer_list<uint8_t> il) {
    reserve(len + il.size());
    for (auto i : il)
      push_back(i);
  }

  size_t size() const { return len; }

  uint8_t* data() const { return buf; }

  uint8_t* begin() { return buf; }
  const uint8_t* begin() const { return buf; }
  const uint8_t* cbegin() const { return buf; }

  uint8_t* end() { return buf + len; }
  const uint8_t* end() const { return buf + len; }
  const uint8_t* cend() const { return buf + len; }

public:
//  template<typename Ret = void, typename... Args>
//  inline Ret call(Args... args) {
//    std::cout << *buf << std::endl;
//    Ret res = ;
//    return res;
//  }

  uint32_t operator()() {
    return reinterpret_cast<uint32_t (*) ()>(buf)();
  }

public:
  exec_buf() = default;
  exec_buf(size_t size) { resize(size); }
  ~exec_buf() { ::munmap(buf, buf_len); }

  exec_buf(exec_buf&& other) {
    buf = other.buf;
    buf_len = other.buf_len;
    len = other.len;

    other.buf = nullptr;
    other.buf_len = 0;
    other.len = 0;
  }
  inline exec_buf& operator=(exec_buf&& other) {
    reset();

    buf = other.buf;
    buf_len = other.buf_len;
    len = other.len;

    other.buf = nullptr;
    other.buf_len = 0;
    other.len = 0;
  }
};

template<typename T>
void vec_hack_back(std::vector<uint8_t>& vec, const T& t) {
  size_t offset = vec.size();
  vec.resize(offset + sizeof(T));
  memcpy(vec.data() + offset, &t, sizeof(T));
}

/// XXX: So bad...
void vec_append(std::vector<uint8_t>& vec, std::initializer_list<uint8_t> il) {
  vec.reserve(vec.size() + il.size());
  for (auto i : il)
    vec.push_back(i);
}


// Oh god this is not portable
extern "C" uint32_t lmc_input() {
  std::cout << "Give me an int: " << std::endl;
  uint32_t res;
  std::cin >> res;
  return res;
}
extern "C" uint32_t lmc_output(uint32_t i) {
  std::cout << "Output: " << i << std::endl;
}

class lmc {
public:
  enum opcode_t {
    Halt = 0,
    Load,
    Store,
    Add,
    Sub,
    Input,
    Output,
    Brz,
    Bra,
    Brp
  };
  using word_t = int32_t;
  using instruction_t = std::pair<opcode_t, word_t>;

public:
  template<typename Iter>
  static std::pair<exec_buf, std::unique_ptr<uint32_t[]>> assemble(Iter begin, Iter end, size_t data_len) {
    // So this is literally the worst thing I have written
    //
    // EAX will be used as the accumilator register
    // ECX will be used as the offset register (i.e. pointing to the start of 32 bit data)
    //
    size_t ins_count = end - begin;
    std::vector<uint32_t> opcode_offsets;
    opcode_offsets.reserve(ins_count);
    std::vector<uint8_t> temp_ins;
    temp_ins.reserve(2*ins_count);
    std::unique_ptr<uint32_t[]> data_buf(new uint32_t[data_len]);

    std::vector<std::optional<uint32_t>> pos_fills;

    for (auto iter = begin; iter != end; ++iter) {
      const instruction_t& i = *iter;
      opcode_offsets.push_back(temp_ins.size());
      switch (i.first) {
        case Halt: {
          temp_ins.push_back(0xC3); // ret
          pos_fills.emplace_back();
        } break;
        case Load: {
          vec_append(temp_ins, {0xa1}); // mov eax, ...
          pos_fills.emplace_back(temp_ins.size());
          vec_hack_back<uint32_t>(temp_ins, 0);
        } break;
        case Store: {
          vec_append(temp_ins, {0xa3});// mov ..., eax
          pos_fills.emplace_back(temp_ins.size());
          vec_hack_back<uint32_t>(temp_ins, 0);
        } break;
        case Input: {
          vec_append(temp_ins, {
                       0x55, // push ebp
                       0x89, 0xe5, // mov ebp, esp
                       0xff, 0x93 // call ...
                     });
          pos_fills.emplace_back(temp_ins.size());
          vec_hack_back<uint32_t>(temp_ins, 0);
          vec_append(temp_ins, {
                       0x89, 0xec, // mov esp, ebp
                       0x89, 0xec, // mov ebp, esp
                       0x5d, // pop ebp
                     });
          pos_fills.emplace_back();
        } break;
        case Output: {
          vec_append(temp_ins, {
                       0x50, // push eax
                       0x55, // push ebp
                       0x89, 0xe5, // mov ebp, esp
                       0x50, // push eax
                       0xff, 0x93 // call ...
                     });
          pos_fills.emplace_back(temp_ins.size());
          vec_hack_back<uint32_t>(temp_ins, 0);
          vec_append(temp_ins, {
                       0x89, 0xec, // mov esp, ebp
                       0x89, 0xec, // mov ebp, esp
                       0x5d, // pop ebp
                       0x58  // pop eax
                     });
          pos_fills.emplace_back();
        } break;
        case Brz: {
          vec_append(temp_ins, {
                       0x83, 0xf8, 0x00, // cmp eax, 0
                       0x0f, 0x84, 0xcd // je ...
                     });
          pos_fills.emplace_back(temp_ins.size());
          vec_append(temp_ins, {0x00, 0x00, 0x00});
        } break;
        case Bra: {
          vec_append(temp_ins, {0xe9, 0xc7});
          pos_fills.emplace_back(temp_ins.size());
          vec_append(temp_ins, {0x00, 0x00, 0x00});
        } break;
        case Brp: {
          vec_append(temp_ins, {
                       0x83, 0xf8, 0x00, // cmp eax, 0
                       0x0f, 0x87, 0xbd // ja ...
                     });
          pos_fills.emplace_back(temp_ins.size());
          vec_append(temp_ins, {0x00, 0x00, 0x00});
        } break;
        case Add: {
          vec_append(temp_ins, {0x03, 0x05});
          pos_fills.emplace_back(temp_ins.size());
          vec_hack_back<uint32_t>(temp_ins, 0);
        } break;
        case Sub: {
          vec_append(temp_ins, {0x2b, 0x05});
          pos_fills.emplace_back(temp_ins.size());
          vec_hack_back<uint32_t>(temp_ins, 0);
        } break;
      }
    }

    exec_buf ret;
    ret.resize(temp_ins.size());
    std::copy(temp_ins.begin(), temp_ins.end(), ret.begin());

    size_t pos = 0;

    for (auto iter = begin; iter != end; ++iter, ++pos) {
      if (!pos_fills[pos])
        continue;
      const instruction_t& i = *iter;
      opcode_offsets.push_back(temp_ins.size());
      switch (i.first) {
        case Brz:
        case Bra:
        case Brp: {
          int32_t caller_addr = static_cast<int32_t>(reinterpret_cast<int64_t>(ret.data() + opcode_offsets.at(pos)));
          int32_t callee_addr = static_cast<int32_t>(reinterpret_cast<int64_t>(ret.data() + opcode_offsets.at(i.second)));
          // Mwah-hah-hah I'm evil
          int32_t val = callee_addr - caller_addr;
          ::memcpy(ret.data() + *pos_fills[pos], &val, 3);
        } break;

        case Input: {
          int32_t caller_addr = static_cast<int32_t>(reinterpret_cast<int64_t>(&lmc_input));
          int32_t callee_addr = static_cast<int32_t>(reinterpret_cast<int64_t>(ret.data() + opcode_offsets.at(i.second)));
          // Mwah-hah-hah I'm evil
          int32_t val = callee_addr - caller_addr;
          ::memcpy(ret.data() + *pos_fills[pos], &val, 3);
        } break;

        case Output: {
          int32_t caller_addr = static_cast<int32_t>(reinterpret_cast<int64_t>(&lmc_output));
          int32_t callee_addr = static_cast<int32_t>(reinterpret_cast<int64_t>(ret.data() + opcode_offsets.at(i.second)));
          // Mwah-hah-hah I'm evil
          int32_t val = callee_addr - caller_addr;
          ::memcpy(ret.data() + *pos_fills[pos], &val, 3);
        } break;

        case Add:
        case Sub:
        case Load:
        case Store: {
          uint32_t val = static_cast<uint32_t>(reinterpret_cast<uint64_t>(data_buf.get() + i.second));
          ::memcpy(ret.data() + *pos_fills[pos], &val, 4);
        } break;
        default: {}
      }
    }

    return {std::move(ret), std::move(data_buf)};
  }
};

int main() {
  std::vector<lmc::instruction_t> ins {
//    {lmc::Input, 0},
    {lmc::Load, 0},
    {lmc::Add, 1},
    {lmc::Halt, 0},
//    {lmc::Output, 0}
  };
  size_t data_size = 10;
  auto e = lmc::assemble(ins.begin(), ins.end(), data_size);
  e.second[0] = 1;
  e.second[1] = 2;

  for (auto i : e.first)
    std::cout << std::hex << std::setfill('0') << std::setw(2) << (int)i;
  std::cout << std::endl;

  uint32_t res = e.first();

  std::cout << "Result: " << res << std::endl;
}
