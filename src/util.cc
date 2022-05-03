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

#include <unistd.h>
#include <fcntl.h>
#include <fstream>
#include <iostream>

#include "glog/logging.h"
#include "google/protobuf/io/zero_copy_stream_impl.h"
#include "google/protobuf/text_format.h"

namespace botc {
using google::protobuf::io::FileInputStream;
using google::protobuf::io::FileOutputStream;
using google::protobuf::TextFormat;
using std::ofstream;

void ReadProtoFromFile(const string& filename, Message* msg) {
  int fi = open(filename.c_str(), O_RDONLY);
  CHECK_NE(fi, -1) << "File not found: " << filename;
  FileInputStream fstream(fi);
  TextFormat::Parse(&fstream, msg);
  close(fi);
}

void WriteProtoToFile(const string& filename, const Message& msg) {
  int fd = open(filename.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0644);
  CHECK_NE(fd, -1) << "Failed opening file: " << filename;
  FileOutputStream* output = new FileOutputStream(fd);
  TextFormat::Print(msg, output);
  output->Flush();
  close(fd);
}

}  // namespace botc
