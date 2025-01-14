Documentation: https://instrumentation.obs.carnegiescience.edu/Software/ZWO/

Building on Docker
------------------

    export GIT_AUTH_TOKEN="GITHUB PERSONAL AUTH TOKEN"
    docker build --secret id=GIT_AUTH_TOKEN . -t zwo
    