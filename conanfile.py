import os
import shutil

from conan import ConanFile
from conan.tools.files import download, unzip, check_sha256, load, save


class AtlasSystemAgentConan(ConanFile):
    settings = "os", "compiler", "build_type", "arch"
    requires = (
        "abseil/20260526.0",
        "asio/1.38.2",
        "backward-cpp/1.6",
        # Capped at 1.90.0: the boost/1.91.0 recipe expects a cobalt_io_ssl library that it
        # never provides OpenSSL for, so package_info() fails. Matches the version the
        # vendored thirdparty/spectator-cpp pins.
        "boost/1.90.0",
        # Pinned to 12.1.0 to match the fmt that spdlog/1.17.0 requires
        "fmt/12.1.0",
        "gtest/1.17.0",
        "libcurl/8.10.1",
        # libcurl/8.10.1 requires openssl/[>=1.1 <4], so 4.x is not usable yet
        "openssl/3.6.3",
        "rapidjson/cci.20250205",
        "sdbus-cpp/2.3.1",
        "spdlog/1.17.0",
        "zlib/1.3.2",
    )
    tool_requires = ()
    generators = "CMakeDeps", "CMakeToolchain"

    def requirements(self):
        # Pin the libsystemd pulled in transitively by sdbus-cpp
        self.requires("libsystemd/255.10", override=True)

    def configure(self):
        self.options["libcurl"].with_c_ares = True
        self.options["libcurl"].with_ssl = "openssl"
        # b2 builds an extra boost_cobalt_io_ssl library whenever it can find OpenSSL, but
        # the boost/1.90.0 recipe does not list it, so package_info() aborts with "built,
        # but were not used in any boost module". This only reproduces where OpenSSL headers
        # are visible, which is why it breaks CI but not every local build. We do not use
        # Cobalt, and disabling it stops b2 from building cobalt/cobalt_io/cobalt_io_ssl.
        self.options["boost"].without_cobalt = True

    @staticmethod
    def maybe_remove_dir(path: str):
        if os.path.isdir(path):
            shutil.rmtree(path)

    @staticmethod
    def maybe_remove_file(path: str):
        if os.path.isfile(path):
            os.unlink(path)

    def get_spectator_cpp(self):
        thirdparty_dir = "thirdparty"
        repo = "Netflix/spectator-cpp"
        commit = "656bf58c5560e83b31918bd71b73cecb2c634014"  # v2.3.0

        zip_path = os.path.join(thirdparty_dir, f"spectator-cpp-{commit}.zip")
        dir_path = os.path.join(thirdparty_dir, "spectator-cpp")

        os.makedirs(thirdparty_dir, exist_ok=True)
        self.maybe_remove_file(zip_path)
        self.maybe_remove_dir(dir_path)

        download(self, f"https://github.com/{repo}/archive/{commit}.zip", zip_path)
        check_sha256(self, zip_path, "22e31a33eb796a91b52cf14ff18d2a29e2dd0be9969e67b8f3130e4dea0a096a")
        unzip(self, zip_path, destination=dir_path, strip_root=True)
        self.maybe_remove_file(zip_path)

    def get_rocm_systems(self):
        thirdparty_dir = "thirdparty"
        repo = "ROCm/rocm-systems"
        tag = "rocm-7.2.3"

        zip_path = os.path.join(thirdparty_dir, f"rocm-systems-{tag}.zip")
        dir_path = os.path.join(thirdparty_dir, "rocm-systems")

        os.makedirs(thirdparty_dir, exist_ok=True)
        self.maybe_remove_file(zip_path)
        self.maybe_remove_dir(dir_path)

        download(self, f"https://github.com/{repo}/archive/refs/tags/{tag}.zip", zip_path)
        check_sha256(self, zip_path, "8874d65b072e0f915b4f334bec7d61fd09ecb3cbd066b58a06afe15365e46338")
        unzip(self, zip_path, destination=dir_path, strip_root=True)
        self.disable_goamdsmi_shim(dir_path)
        self.maybe_remove_file(zip_path)

    def disable_goamdsmi_shim(self, rocm_dir: str):
        # AMD SMI unconditionally builds its Go shim (goamdsmi_shim -> libgoamdsmi_shim64.so) via a
        # bare add_subdirectory() with no gating option. The shim hardcodes the x86-only flags
        # -m64/-msse/-msse2 with no architecture guard, so on aarch64 it fails to link
        # ("unrecognized command-line option '-m64'") and breaks the whole build. We only ever link
        # libamd_smi.a (whose own x86 flags ARE arch-guarded, so it builds on every arch), so
        # comment out the line that adds the shim.
        cmakelists = os.path.join(rocm_dir, "projects", "amdsmi", "CMakeLists.txt")
        marker = "add_subdirectory(goamdsmi_shim)"
        note = "disabled by atlas-system-agent: unused Go shim, x86-only flags break aarch64"

        contents = load(self, cmakelists)
        if note in contents:
            return  # already patched

        lines = contents.splitlines(keepends=True)
        for i, line in enumerate(lines):
            if line.strip() == marker:
                newline = "\n" if line.endswith("\n") else ""
                lines[i] = "# {}  # {}{}".format(marker, note, newline)
                save(self, cmakelists, "".join(lines))
                self.output.info("Disabled goamdsmi_shim build (aarch64 compatibility)")
                return

        self.output.warning(
            "Could not find '{}' in {}; amdsmi layout may have changed".format(marker, cmakelists))

    def source(self):
        self.get_spectator_cpp()
        self.get_rocm_systems()