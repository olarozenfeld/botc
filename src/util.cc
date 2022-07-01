// Copyright 2022 Ola Rozenfeld
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
#include "src/util.h"

#include <fcntl.h>
#include <fstream>
#include <iostream>

#ifdef _WIN32
#include <io.h>
#endif

#include "ortools/base/logging.h"
#include "google/protobuf/io/zero_copy_stream_impl.h"
#include "google/protobuf/text_format.h"

namespace botc {
using google::protobuf::io::FileInputStream;
using google::protobuf::io::FileOutputStream;
using google::protobuf::TextFormat;
using std::ofstream;

void ReadProtoFromFile(const path& filename, Message* msg) {
  int ff = open(filename.string().c_str(), O_RDONLY);
  CHECK_GT(ff, 0) << "File opening file: " << filename;
  FileInputStream fstream(ff);
  CHECK(TextFormat::Parse(&fstream, msg)) << "Failed parsing proto from " << filename;
  close(ff);
}

void WriteProtoToFile(const Message& msg, const path& filename) {
  int ff = open(filename.string().c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0644);
  CHECK_GT(ff, 0) << "Failed opening file: " << filename;
  FileOutputStream* output = new FileOutputStream(ff);
  TextFormat::Print(msg, output);
  output->Flush();
  close(ff);
}

}  // namespace botc
