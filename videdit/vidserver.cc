// Copyright 2018 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "base/bind.h"
#include "base/files/file_path.h"
#include "base/logging.h"
#include "ui/views_content_client/views_content_client.h"
#include "net/test/embedded_test_server/embedded_test_server.h"

namespace {

class VidServer {
 public:
  VidServer() {
    //net::SpawnedTestServer spawn(net::SpawnedTestServer::TYPE_HTTP, root);
    server.AddDefaultHandlers(base::FilePath("videdit"));
    bool result = server.Start();
    assert(result);
    LOG(INFO) << "Serving at " << server.host_port_pair().ToString();
  }

 private:
  net::EmbeddedTestServer server{net::EmbeddedTestServer::TYPE_HTTP};
};

VidServer* g_vid_server;

void Start(content::BrowserContext* browser_context,
           gfx::NativeWindow window_context) {
  DLOG(INFO) << "Start task.";
  g_vid_server = new VidServer;
}

}  // namespace

int main(int argc, const char** argv) {
  ui::ViewsContentClient views_content_client(argc, argv);
  views_content_client.set_task(base::Bind(&Start));
  return views_content_client.RunMain();
}
