Documentation: https://instrumentation.obs.carnegiescience.edu/Software/ZWO/

Importing to git
----------------

    git init
    
    # HTML
    git add -A html/ZWO/
    git commit --author="Christoph Birk <birk@carnegiescience.edu>" -m "HTML documentation"
    
    # Server
    rsync -av ZWO/Server/src-0031/ server/
    git add -A server/
    git commit --author="Christoph Birk <birk@carnegiescience.edu>" -m "server v0031 (2022-08-24)"
    rsync -av ZWO/Server/src-0032/ server/
    git add -A server/
    git commit --author="Christoph Birk <birk@carnegiescience.edu>" -m "server v0032 (2022-08-26)"
    rsync -av ZWO/Server/src-0033/ server/
    git add -A server/
    git commit --author="Christoph Birk <birk@carnegiescience.edu>" -m "server v0033 (2023-03-21)"
    rsync -av --delete ZWO/Server/src-0034/ server/
    git add -A server/
    git commit --author="Christoph Birk <birk@carnegiescience.edu>" -m "server v0034 (2023-04-30)"
    
    # Finder
    rsync -av --delete ZWO/Finder/ZWOFinder-0040/ finder/
    git add -A finder/
    git commit --author="Christoph Birk <birk@carnegiescience.edu>" -m "finder v0040 (2021-04-14)"
    rsync -av --delete ZWO/Finder/ZWOFinder-0041/ finder/
    git add -A finder/
    git commit --author="Christoph Birk <birk@carnegiescience.edu>" -m "finder v0041 (2021-04-15)"
    rsync -av --delete ZWO/Finder/ZWOFinder-0042/ finder/
    git add -A finder/
    git commit --author="Christoph Birk <birk@carnegiescience.edu>" -m "finder v0042 (2021-07-08)"
    rsync -av --delete ZWO/Finder/ZWOFinder-0044/ finder/
    git add -A finder/
    git commit --author="Christoph Birk <birk@carnegiescience.edu>" -m "finder v0044 (2024-03-04)"

    # Guider
    rsync -av --delete ZWO/src-0350/ guider/
    git add -A guider/
    git commit --author="Christoph Birk <birk@carnegiescience.edu>" -m "guider v0.350 (2023-12-17)"
    rsync -av --delete ZWO/src-0351/ guider/
    git add -A guider/
    git commit --author="Christoph Birk <birk@carnegiescience.edu>" -m "guider v0.351 (2023-12-18)"
    rsync -av --delete ZWO/src-0352/ guider/
    git add -A guider/
    git commit --author="Christoph Birk <birk@carnegiescience.edu>" -m "guider v0.352 (2023-12-18)"
    rsync -av --delete ZWO/src-0353/ guider/
    git add -A guider/
    git commit --author="Christoph Birk <birk@carnegiescience.edu>" -m "guider v0.353 (2023-12-19)"
    rsync -av --delete ZWO/src-0354/ guider/
    git add -A guider/
    git commit --author="Christoph Birk <birk@carnegiescience.edu>" -m "guider v0.354 (2023-12-20)"
    rsync -av --delete ZWO/src-0355/ guider/
    git add -A guider/
    git commit --author="Christoph Birk <birk@carnegiescience.edu>" -m "guider v0.355 (2023-12-21)"
    rsync -av --delete ZWO/src-0401/ guider/
    git add -A guider/
    git commit --author="Christoph Birk <birk@carnegiescience.edu>" -m "guider v0.401 (2024-01-09)"
    rsync -av --delete ZWO/src-0402/ guider/
    git add -A guider/
    git commit --author="Christoph Birk <birk@carnegiescience.edu>" -m "guider v0.402 (2024-01-11)"
    rsync -av --delete ZWO/src-0403/ guider/
    git add -A guider/
    git commit --author="Christoph Birk <birk@carnegiescience.edu>" -m "guider v0.403 (2024-01-12)"
    rsync -av --delete ZWO/src-0404/ guider/
    git add -A guider/
    git commit --author="Christoph Birk <birk@carnegiescience.edu>" -m "guider v0.404 (2024-01-22)"
    rsync -av --delete ZWO/src-0405/ guider/
    git add -A guider/
    git commit --author="Christoph Birk <birk@carnegiescience.edu>" -m "guider v0.405 (2024-01-23)"
    rsync -av --delete ZWO/src-0406/ guider/
    git add -A guider/
    git commit --author="Christoph Birk <birk@carnegiescience.edu>" -m "guider v0.406 (2024-01-24)"
    rsync -av --delete ZWO/src-0407/ guider/
    git add -A guider/
    git commit --author="Christoph Birk <birk@carnegiescience.edu>" -m "guider v0.407 (2024-01-25)"
    rsync -av --delete ZWO/src-0408/ guider/
    git add -A guider/
    git commit --author="Christoph Birk <birk@carnegiescience.edu>" -m "guider v0.408 (2024-01-29)"
    rsync -av --delete ZWO/src-0409/ guider/
    git add -A guider/
    git commit --author="Christoph Birk <birk@carnegiescience.edu>" -m "guider v0.409 (2024-01-30)"
    rsync -av --delete ZWO/src-0410/ guider/
    git add -A guider/
    git commit --author="Christoph Birk <birk@carnegiescience.edu>" -m "guider v0.410 (2024-01-30)"
    rsync -av --delete ZWO/src-0411/ guider/
    git add -A guider/
    git commit --author="Christoph Birk <birk@carnegiescience.edu>" -m "guider v0.411 (2024-01-31)"
    rsync -av --delete ZWO/src-0412/ guider/
    git add -A guider/
    git commit --author="Christoph Birk <birk@carnegiescience.edu>" -m "guider v0.412 (2024-02-08)"
    rsync -av --delete ZWO/src-0413/ guider/
    git add -A guider/
    git commit --author="Christoph Birk <birk@carnegiescience.edu>" -m "guider v0.413 (2024-02-09)"
    rsync -av --delete ZWO/src-0414/ guider/
    git add -A guider/
    git commit --author="Christoph Birk <birk@carnegiescience.edu>" -m "guider v0.414 (2024-02-12)"
    rsync -av --delete ZWO/src-0415/ guider/
    git add -A guider/
    git commit --author="Christoph Birk <birk@carnegiescience.edu>" -m "guider v0.415 (2024-02-20)"
    rsync -av --delete ZWO/src-0417/ guider/
    git add -A guider/
    git commit --author="Christoph Birk <birk@carnegiescience.edu>" -m "guider v0.417 (2024-03-05)"
    rsync -av --delete ZWO/src-0418/ guider/
    git add -A guider/
    git commit --author="Christoph Birk <birk@carnegiescience.edu>" -m "guider v0.418 (2024-03-08)"
    rsync -av --delete ZWO/src-0419/ guider/
    git add -A guider/
    git commit --author="Christoph Birk <birk@carnegiescience.edu>" -m "guider v0.419 (2024-03-11)"
    rsync -av --delete ZWO/src-0420/ guider/
    git add -A guider/
    git commit --author="Christoph Birk <birk@carnegiescience.edu>" -m "guider v0.420 (2024-03-12)"
    rsync -av --delete ZWO/src-0421/ guider/
    git add -A guider/
    git commit --author="Christoph Birk <birk@carnegiescience.edu>" -m "guider v0.421 (2024-03-12)"
    rsync -av --delete ZWO/src-0422/ guider/
    git add -A guider/
    git commit --author="Christoph Birk <birk@carnegiescience.edu>" -m "guider v0.422 (2024-04-08)"
    rsync -av --delete ZWO/src-0423/ guider/
    git add -A guider/
    git commit --author="Christoph Birk <birk@carnegiescience.edu>" -m "guider v0.423 (2024-05-10)"
    rsync -av --delete ZWO/src-0424/ guider/
    git add -A guider/
    git commit --author="Christoph Birk <birk@carnegiescience.edu>" -m "guider v0.424 (2024-05-14)"
    rsync -av --delete ZWO/src-0425/ guider/
    git add -A guider/
    git commit --author="Christoph Birk <birk@carnegiescience.edu>" -m "guider v0.425 (2024-10-18)"
    rsync -av --delete ZWO/src-0426/ guider/
    git add -A guider/
    git commit --author="Christoph Birk <birk@carnegiescience.edu>" -m "guider v0.426 (2024-11-20)"
    rsync -av --delete ZWO/src-0427/ guider/
    git add -A guider/
    git commit --author="Christoph Birk <birk@carnegiescience.edu>" -m "guider v0.427 (2024-12-02)"