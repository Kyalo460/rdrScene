$ErrorActionPreference = "Continue"
$env:EMSDK = "C:\Users\USER\rdrScene\emsdk"
$env:EMSDK_NODE = "C:\Users\USER\rdrScene\emsdk\node\24.19.0_64bit\node.exe"
$env:EMSDK_PYTHON = "C:\Users\USER\rdrScene\emsdk\python\3.13.3_64bit\python.exe"
$env:PATH = "C:\Users\USER\rdrScene\emsdk;C:\Users\USER\rdrScene\emsdk\upstream\emscripten;" + $env:PATH

Set-Location "C:\Users\USER\rdrScene\nairobi_life"
$py = "C:\Users\USER\rdrScene\emsdk\python\3.13.3_64bit\python.exe"
$emcc = "C:\Users\USER\rdrScene\emsdk\upstream\emscripten\emcc.py"

$args = @(
  $emcc,
  "src/main.c","src/nl_util.c","src/nl_time.c","src/nl_weather.c","src/nl_world.c","src/nl_npc.c","src/nl_econ.c","src/nl_render.c","src/nl_game.c",
  "C:\tmp\raylib-src\src\rcore.c","C:\tmp\raylib-src\src\rshapes.c","C:\tmp\raylib-src\src\rtextures.c","C:\tmp\raylib-src\src\rtext.c","C:\tmp\raylib-src\src\rmodels.c","C:\tmp\raylib-src\src\raudio.c","C:\tmp\raylib-src\src\utils.c","C:\tmp\raylib-src\src\rglfw.c",
  "-o","nairobi_life.html",
  "-std=gnu99","-Wall","-Wextra","-O2","-Isrc","-Ic:/tmp/raylib-src/src","-DPLATFORM_WEB=1",
  "-s","USE_GLFW=3",
  "-s","ASYNCIFY=1",
  "-s","TOTAL_MEMORY=67108864",
  "-s","FORCE_FILESYSTEM=1",
  "-s","ALLOW_MEMORY_GROWTH=1",
  "-s","WARN_ON_UNDEFINED_SYMBOLS=0",
  "-s",'EXPORTED_RUNTIME_METHODS=["ccall","cwrap"]',
  "-s",'EXPORTED_FUNCTIONS=["_main","_malloc","_free"]',
  "--shell-file","C:\Users\USER\rdrScene\nairobi_life\web_shell.html"
)

"== emcc build start: $(Get-Date -Format o) ==" | Out-File -Encoding utf8 "web_build.log"
& $py $args *>> "web_build.log"
$code = $LASTEXITCODE
"== emcc exit code: $code at $(Get-Date -Format o) ==" | Out-File -Append -Encoding utf8 "web_build.log"
"BUILD_DONE_EXIT_$code" | Out-File -Append -Encoding utf8 "web_build.log"
