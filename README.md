Documentation: https://instrumentation.obs.carnegiescience.edu/Software/ZWO/

Building on Docker
------------------

    export GIT_AUTH_TOKEN="GITHUB PERSONAL AUTH TOKEN"
    docker build --secret id=GIT_AUTH_TOKEN . -t zwo

"Testing" with Docker
---------------------

    docker run -it zwo
    LD_LIBRARY_PATH=/usr/local/lib/ /app/zwo/src/server/zwoserver &
    telnet localhost 52311
