

### 部署K8S-nightcore-benchmark

```shell
#下载nightcore-benchmark
git clone https://github.com/CIBao/nightcore-benchmarks.git
#备用 git clone https://hub.fastgit.org/CIBao/nightcore-benchmarks.git
cd nightcore-benchmarks/experiments/socialnetwork_singlenode/



#设置配置文件
sudo rm -rf /tmp/socialNetwork
mkdir -p /tmp/socialNetwork
sudo rm -rf /mnt/inmem/db
sudo mkdir -p /mnt/inmem/db
sudo rm -rf /mnt/inmem/nightcore
sudo mkdir -p /mnt/inmem/nightcore
sudo mkdir -p /mnt/inmem/nightcore/output /mnt/inmem/nightcore/ipc

cp ./nightcore_config.json /tmp/nightcore_config.json

#修改 service-config.json
#将“addr”添加 .nightcore-sn.svc.cluster.local
cp ./service-config.json /tmp/service-config.json

cp ./run_launcher /tmp/run_launcher
sudo cp /tmp/run_launcher /mnt/inmem/nightcore/run_launcher
sudo cp /tmp/nightcore_config.json /mnt/inmem/nightcore/func_config.json

#修改Nginx-k8s.conf和media-front/nginx.conf
#将resolver改为10.96.0.10(kube-dns.kube-system.svc.cluster.local)

cp -r ../../workloads/DeathStarBench/socialNetwork/nginx-web-server    /tmp/socialNetwork
cp -r ../../workloads/DeathStarBench/socialNetwork/media-frontend     /tmp/socialNetwork
cp -r ../../workloads/DeathStarBench/socialNetwork/gen-lua             /tmp/socialNetwork
cp -r ../../workloads/DeathStarBench/socialNetwork/docker              /tmp/socialNetwork

cp  ../../workloads/DeathStarBench/socialNetwork/scripts/init_social_graph.py                  ~
cp  ../../workloads/DeathStarBench/socialNetwork/wrk2_scripts/compose-post.lua           ~
cp  ../../workloads/DeathStarBench/socialNetwork/datasets/social-graph/socfb-Reed98/socfb-Reed98.mtx    ~

#根据docker-compose-write编写k8s-yaml
#可以利用kompose convert生成yaml，作为参考
#setup：nightcore-sn命名空间
#backend： memcache、redis和mongodb等文件 
#						socialnetwork-mongodb.yaml中注意db位置/mnt/inmem/db（权限问题）改为/home/k8s/db
#                       nightcore-gateway：其中service开放8080端口（For http）和10007端口（For connect nightcore-gateway ）
#剩余为service文件
#修改nightcore-engine.yaml 
#--gateway_addr=nightcore-gateway.nightcore-sn.svc.cluster.local

sudo rm -rf ~/db
mkdir ~/db

#部署
cd k8s-yaml   
kubectl apply -f  setup/
kubectl apply -f backend/
kubectl apply -f .

#wrk测试
cd /home/k8s/exper/ywq/nightcore-benchmarks/misc/wrk2/
make

#初始化
python3 init_social_graph.pyhttp://localhost:31234 socfb-Reed98.mtx #(其中端口为nginx-thrift的svc nodeport)


#退出
kubectl delete namespace nightcore-sn

```

