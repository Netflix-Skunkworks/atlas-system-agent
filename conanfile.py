import os
import shutil

from conans import ConanFile
from conans.tools import download, unzip, check_sha256


class SpectatorDConan(ConanFile):
    settings = "os", "compiler", "build_type", "arch"
    requires = (
        "abseil/20210324.2",
        "asio/1.18.1",  # for spectator-cpp
        "backward-cpp/1.6",
        "c-ares/1.15.0",
        "fmt/7.1.3",
        "gtest/1.10.0",
        "libcurl/7.87.0",
        "openssl/1.1.1t",
        "rapidjson/1.1.0",
        "spdlog/1.8.5",
        "zlib/1.2.13"
    )
    generators = "cmake"
    default_options = {}

    @staticmethod
    def get_spectator_cpp():
        dir_name = "spectator-cpp"
        commit = "518797fee3593c0a6a155076dae2dc6f87ef0d43"
        if os.path.isdir(dir_name):
            shutil.rmtree(dir_name)
        zip_name = f"spectator-cpp-{commit}.zip"
        download(f"https://github.com/Netflix/spectator-cpp/archive/{commit}.zip", zip_name)
        check_sha256(zip_name, "f2a5d3438a072dfe505a195fdd77bee70867beb61650c0fd36a828982c218c0d")
        unzip(zip_name)
        shutil.move(f"spectator-cpp-{commit}/spectator", "lib/spectator")
        shutil.rmtree(f"spectator-cpp-{commit}")
        os.unlink(zip_name)

    def source(self):
        self.get_spectator_cpp()
