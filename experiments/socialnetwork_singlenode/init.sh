sudo rm -rf /tmp/socialNetwork
mkdir -p /tmp/socialNetwork
sudo rm -rf /mnt/inmem/db
sudo mkdir -p /mnt/inmem/db
sudo rm -rf /mnt/inmem/nightcore
sudo mkdir -p /mnt/inmem/nightcore
sudo mkdir -p /mnt/inmem/nightcore/output /mnt/inmem/nightcore/ipc

cp ./nightcore_config.json /tmp/nightcore_config.json
cp ./service-config.json /tmp/service-config.json

cp ./run_launcher /tmp/run_launcher
sudo cp /tmp/run_launcher /mnt/inmem/nightcore/run_launcher
sudo cp /tmp/nightcore_config.json /mnt/inmem/nightcore/func_config.json

cp -r ~/tmp/nightcore-benchmarks/workloads/DeathStarBench/socialNetwork/nginx-web-server    /tmp/socialNetwork
cp -r ~/tmp/nightcore-benchmarks/workloads/DeathStarBench/socialNetwork/media-frontend     /tmp/socialNetwork
cp -r ~/tmp/nightcore-benchmarks/workloads/DeathStarBench/socialNetwork/gen-lua             /tmp/socialNetwork
cp -r ~/tmp/nightcore-benchmarks/workloads/DeathStarBench/socialNetwork/docker              /tmp/socialNetwork

cp  ~/tmp/nightcore-benchmarks/workloads/DeathStarBench/socialNetwork/scripts/init_social_graph.py                  ~
cp  ~/tmp/nightcore-benchmarks/workloads/DeathStarBench/socialNetwork/wrk2_scripts/compose-post.lua           ~
cp  ~/tmp/nightcore-benchmarks/workloads/DeathStarBench/socialNetwork/datasets/social-graph/socfb-Reed98/socfb-Reed98.mtx    ~