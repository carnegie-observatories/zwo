Documentation: https://instrumentation.obs.carnegiescience.edu/Software/ZWO/

Building on Docker
------------------

    export GIT_AUTH_TOKEN="GITHUB PERSONAL AUTH TOKEN"
    docker build --secret id=GIT_AUTH_TOKEN . -t ghcr.io/carnegie-observatories/zwo:latest

"Testing" with Docker
---------------------

    docker run -it zwo
    LD_LIBRARY_PATH=/usr/local/lib/ /app/zwo/src/server/zwoserver &
    telnet localhost 52311

GCAM on Docker
--------------

To build and run the GCAM container, use the following commands:

    docker-compose build gcam
    docker-compose up gcam

To access the GCAM container over VNC, open a browser and navigate to:

    http://localhost:3000/

