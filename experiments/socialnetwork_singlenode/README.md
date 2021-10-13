

### 部署K8S-nightcore-benchmark

```shell
#下载nightcore-benchmark
git clone https://github.com/CIBao/nightcore-benchmarks.git
#备用 git clone https://hub.fastgit.org/CIBao/nightcore-benchmarks.git
cd nightcore-benchmarks/experiments/socialnetwork_singlenode/



./init.sh

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

#初始化
python3 init_social_graph.pyhttp://localhost:31234 socfb-Reed98.mtx #(其中端口为nginx-thrift的svc nodeport)


#wrk测试
cd ../../misc/wrk2/
make
./wrk -t 4 -c 48 -d 150 -L -U  -s ~/compose-post.lua  http://localhost:8080 -R 1000

#退出
kubectl delete namespace nightcore-sn

```

