import os
import shutil

from conans import ConanFile
from conans.tools import download, unzip, check_sha256


class AtlasSystemAgentConan(ConanFile):
    settings = "os", "compiler", "build_type", "arch"
    requires = (
        "abseil/20230125.3",
        "asio/1.28.1",
        "backward-cpp/1.6",
        "fmt/10.1.1",
        "gtest/1.14.0",
        "libcurl/8.2.1",
        "openssl/3.1.3",
        "rapidjson/cci.20230929",
        "spdlog/1.12.0",
        "zlib/1.3"
    )
    generators = "cmake"
    default_options = {}

    def configure(self):
        self.options["libcurl"].with_c_ares = True

    @staticmethod
    def get_spectator_cpp():
        dir_name = "spectator-cpp"
        commit = "b3b93d6d86aa763a2ee409c52cca41be98cda140"
        if os.path.isdir(dir_name):
            shutil.rmtree(dir_name)
        zip_name = f"spectator-cpp-{commit}.zip"
        download(f"https://github.com/Netflix/spectator-cpp/archive/{commit}.zip", zip_name)
        check_sha256(zip_name, "b76d56722bc2e6a3fafe1950019755a7c236f17469af74e71a673168c6d2c88c")
        unzip(zip_name)
        shutil.move(f"spectator-cpp-{commit}/spectator", "lib/spectator")
        shutil.rmtree(f"spectator-cpp-{commit}")
        os.unlink(zip_name)

    def source(self):
        self.get_spectator_cpp()
