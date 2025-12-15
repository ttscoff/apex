# Homebrew formula for Apex
# To use this formula, create a tap:
#   brew tap ttscoff/thelab https://github.com/ttscoff/homebrew-thelab
# Then install:
#   brew install apex

class Apex < Formula
  desc 'Unified Markdown processor supporting CommonMark, GFM, MultiMarkdown, and Kramdown'
  homepage 'https://github.com/ApexMarkdown/apex'
  url 'https://github.com/ApexMarkdown/apex.git',
      tag: 'v0.1.28',
      revision: '03bf69833a9a30d3f4f7b80c6cf65f3cfffa701a'
  version '0.1.28'
  license 'MIT'

  depends_on 'cmake' => :build

  # On macOS, Xcode command line tools are usually sufficient
  # but we can require Xcode if needed for framework builds
  on_macos do
    depends_on xcode: :build
  end

  def install
    # Configure with CMake in a separate build directory
    system 'cmake', '-S', '.', '-B', 'build',
           '-DCMAKE_BUILD_TYPE=Release',
           '-DCMAKE_POLICY_VERSION_MINIMUM=3.5'

    # Build CLI executable (not framework)
    # Man page is pre-generated in the repository
    system 'cmake', '--build', 'build', '--target', 'apex_cli'

    # Install binary and man page
    bin.install 'build/apex'
    man1.install 'man/apex.1'
  end

  test do
    # Test basic functionality
    (testpath / 'test.md').write("# Hello World\n")
    assert_match '<h1 id="hello-world">Hello World</h1>', shell_output("#{bin}/apex test.md")

    # Test version
    assert_match version.to_s, shell_output("#{bin}/apex --version", 2)
  end
end
