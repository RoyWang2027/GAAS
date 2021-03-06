
#ifndef GLOBAL_OPTIMIZATION_GRAPH_H
#define GLOBAL_OPTIMIZATION_GRAPH_H

#include <g2o/core/block_solver.h>
#include <g2o/core/optimization_algorithm_gauss_newton.h>
#include <g2o/core/optimization_algorithm_levenberg.h>
#include <g2o/solvers/linear_solver_eigen.h>
#include <g2o/core/robust_kernel_impl.h>
#include <g2o/core/optimizable_graph.h>
#include "G2OTypes.h"
#include <opencv2/core/persistence.hpp>
#include <memory>
#include <iostream>
#include <ros/ros.h>
#include <sensor_msgs/NavSatFix.h>
#include <nav_msgs/Odometry.h>
#include <geometry_msgs/PoseStamped.h>

#include "GPSExpand.h"
#include "CallbacksBufferBlock.h"
#include <cmath>
#include <deque>
#include <opencv2/opencv.hpp>
using namespace std;
using namespace ygz;
//采用自顶向下设计方法,分块逐步实现.
//先实现AHRS+GPS.
//测试.
//再支持SceneRetrieve.重点是实现3个scene适配变换.
//最后加入SLAM和速度估计部分,实现优化图.


//约定坐标系:全局坐标使用NED坐标系.
//局部坐标系采用机身坐标系.SLAM和回环对齐到这个坐标系.


//struct State//the prev-state of drone.
//{
//    
//};
typedef VertexPR State;
typedef VertexSpeed Speed;
const int GPS_INIT_BUFFER_SIZE = 100;
using namespace ygz;
class GlobalOptimizationGraph
{
public:
    GlobalOptimizationGraph(int argc,char** argv);
    
    bool checkAHRSValid();
    bool checkSLAMValid();
    bool checkQRValid();
    bool checkGPSValid(const sensor_msgs::NavSatFix& gps);
    
    bool init_AHRS(const nav_msgs::Odometry& AHRS_msg);
    bool init_gps();//init longitude,latitude,altitude.
    bool init_SLAM();//match Coordinate.
    //TODO:Init a GPS callback buffer block class,
    //inherit callback buffer base,implement init and check avail.

    bool inputGPS(const sensor_msgs::NavSatFix& gps);
    bool tryInitVelocity();//参考ygz的EdgePRV.
    inline bool isWorkingWithGPS()
    {
        return(this->allow_gps_usage&& this->gps_init_success);
    }
    void addBlockAHRS(const nav_msgs::Odometry& AHRS_msg
        
    );
    //SLAM msg as fixed vertexpr node.
    void addBlockSLAM(const geometry_msgs::PoseStamped& SLAM_msg);
    

    void addBlockGPS(const sensor_msgs::NavSatFix& GPS_msg);
    void addBlockQRCode();
    void addBlockSceneRetriever();
    void addBlockFCAttitude();
    
    //SLAM msg as edge prv and vertexspeed.
    std::deque<nav_msgs::Odometry> slam_prv_buffer;    
    void addSLAM_edgeprv(const geometry_msgs::PoseStamped& SLAM_msg);
    void clearSLAM_edgeprv_buffer()
    {
        this->slam_prv_buffer.clear();
    }
    
    
    void doOptimization()
    {
        //When DEBUG:
        this->debug_show_optimization_graph();//just show how this optimizable_graph looks like.
        
         //TODO
        this->optimizer.initializeOptimization();
        this->optimizer.optimize(10);
        /*this->historyStatesWindow.push_front(currentState);
        //this->historyStatesWindow.reserve();///...
        */
        //clear optimizer and reset!
        this->resetOptimizationGraph();
        cout<<"DEBUG:end of do optimization():"<<endl;
        this->debug_show_optimization_graph();
    }

    void resetOptimizationGraph()
    {
        this->optimizer.clear();
        this->resetNewestVertexPRID();
        //this->optimizer = g2o::SparseOptimizer();
        linearSolver = new g2o::LinearSolverEigen<g2o::BlockSolverX::PoseMatrixType>();
        solver_ptr = new g2o::BlockSolverX(linearSolver);
        solver = new g2o::OptimizationAlgorithmLevenberg(solver_ptr);
        
        optimizer.setAlgorithm(solver);
        optimizer.setVerbose(true);//reset to init state.
        VertexPR* pcurrent_state = new VertexPR;
        pcurrent_state->setId(this->getNewestVertexSpeedID());
        optimizer.addVertex(pcurrent_state);
    }
    bool SpeedInitialization();
    bool estimateCurrentSpeed();
    void initBuffers(CallbackBufferBlock<geometry_msgs::PoseStamped> & SLAMbuf,
                     CallbackBufferBlock<sensor_msgs::NavSatFix>& GPSbuf,
                     CallbackBufferBlock<nav_msgs::Odometry>& AHRSbuf
    )
    {
        this->pSLAM_Buffer = &SLAMbuf;
        this->pGPS_Buffer = &GPSbuf;
        this->pAHRS_Buffer = &AHRSbuf;
    }
    shared_ptr<VertexPR> getpCurrentPR()
    {
        return this->pCurrentPR;
    }
private:
    void debug_show_optimization_graph()
    {
        cout<<"Size of vertices:"<<this->optimizer.vertices().size()<<endl;//size of vertices.
        cout<<"Size of Edges:"<<this->optimizer.edges().size()<<endl;//size of edges.
    }
    cv::FileStorage fSettings;
    //status management.
    

public:
    static const int STATUS_NO_GPS_NO_SCENE = 0; // a bit map.
    static const int STATUS_NO_GPS_WITH_SCENE = 1;
    static const int STATUS_WITH_GPS_NO_SCENE = 2;
    static const int STATUS_GPS_SCENE = 3;
    int GPS_AVAIL_MINIMUM;
    inline int getStatus()
    {
        return this->status;
    }
    void stateTransfer(int new_state);
private:
    int status = STATUS_NO_GPS_NO_SCENE;
    
    //message buffers.
    
    CallbackBufferBlock<geometry_msgs::PoseStamped> * pSLAM_Buffer;
    CallbackBufferBlock<sensor_msgs::NavSatFix> * pGPS_Buffer;
    CallbackBufferBlock<nav_msgs::Odometry> * pAHRS_Buffer;
    //uav location attitude info management.
    

    
    std::deque<State> historyStatesWindow;
    State currentState;

    Speed currentSpeed;
    std::deque<Speed> historySpeed;
    void resetSpeed();
    void marginalizeAndAddCurrentFrameToHistoryState()
    {
        int max_window_size = (this->fSettings)["OPTIMIZATION_GRAPH_KF_WIND_LEN"];
        if(this->historyStatesWindow.size() > max_window_size)
        {
            State p = historyStatesWindow.back();
            p.setFixed(1);//set this fixed. remove it from optimizable graph.
            this->historyStatesWindow.pop_back();//delete last one.
        }
        //TODO:fix speed window. the length of speed window shall be ( len(position) - 1).
        this->historyStatesWindow.push_front(this->currentState);
        //TODO: how do we set new state?
        State new_state;
        this->currentState = new_state;
    }
    void autoInsertSpeedVertexAfterInsertCurrentVertexPR()
    {
        if(this->historyStatesWindow.size()<2)
        {
            return;//nothing to do.
        }
        Speed* pNewSpeed = new Speed();
        State s = currentState;
        
        pNewSpeed->setEstimate(s.t() - this->historyStatesWindow[1].t());
        //ygz::EdgePRV* pNewEdgePRV = new ygz::EdgePRV();
        //pEdgePRV->setMeasurement();//TODO:set slam info into this edge.
    }
    int vertexID = 0;
    int newestVertexPR_id = 0;
    int iterateNewestVertexPRID()
    {
        int retval = this->vertexID;
        this->vertexID++;
        this->newestVertexPR_id = retval;
        return retval;
    }
    int resetNewestVertexPRID()
    {
        int retval = this->vertexID;
        this->vertexID = 0;
        return retval;
    }
    int getNewestVertexSpeedID()
    {
        int retval = this->vertexID;
        this->vertexID++;
        return retval;
    }
    
    //SLAM
    //VertexPR SLAM_to_UAV_coordinate_transfer;
    Matrix3d SLAM_to_UAV_coordinate_transfer_R;
    Vector3d SLAM_to_UAV_coordinate_transfer_t;
    //AHRS
    Matrix3d ahrs_R_init;

    //optimization graph.
    //G2OOptimizationGraph graph;
    g2o::SparseOptimizer optimizer;
    g2o::BlockSolverX::LinearSolverType * linearSolver;
    g2o::BlockSolverX* solver_ptr;
    g2o::OptimizationAlgorithmLevenberg *solver;

    //vector<shared_ptr<g2o::optimizable_graph::Vertex> > VertexVec;
    //vector<shared_ptr<g2o::optimizable_graph::Edge> > EdgeVec;

    shared_ptr<VertexPR> pCurrentPR;
    //gps configuration and initiation.
    void remap_UAV_coordinate_with_GPS_coordinate();
    GPSExpand GPS_coord;

    bool allow_gps_usage = true;//can be configured by config.yaml
    bool gps_init_success = false;
    double gps_init_longitude,gps_init_latitude,gps_init_altitude;

    double gps_init_lon_variance,gps_init_lat_variance,gps_init_alt_variance;
    std::vector<sensor_msgs::NavSatFix> gps_info_buffer;
    //scene part.
    void fix_scene_Rotation_with_AHRS();
    void remap_scene_coordinate_with_GPS_coordinate();
    void remap_scene_coordinate_with_UAV_coordinate();
    void try_reinit_gps()
    {
        ;
    }
    VertexPR relative_scene_to_UAV_body;
};

GlobalOptimizationGraph::GlobalOptimizationGraph(int argc,char** argv)
{
    //cv::FileStorage fSettings;//(string(argv[1]),cv::FileStorage::READ);
    this->fSettings.open(string(argv[1]),cv::FileStorage::READ);
    this->GPS_AVAIL_MINIMUM = fSettings["GPS_AVAIL_MINIMUM"];
    linearSolver = new g2o::LinearSolverEigen<g2o::BlockSolverX::PoseMatrixType>();
    solver_ptr = new g2o::BlockSolverX(linearSolver);
    solver = new g2o::OptimizationAlgorithmLevenberg(solver_ptr);
    //this->optimizer = g2o::SparseOptimizer();
    optimizer.setAlgorithm(solver);
    optimizer.setVerbose(true);
    //optimizer.setVertex();
    /*currentState.setEstimate(currentState);
    currentState.setId(0);//vertex 0 in optimization graph.*/
    //currentState.setId(0);
    //optimizer.addVertex(&currentState);
}
bool GlobalOptimizationGraph::init_AHRS(const nav_msgs::Odometry& AHRS_msg)
{
    auto q = AHRS_msg.pose.pose.orientation;
    Eigen::Quaterniond q_;
    q_.w() = q.w;
    q_.x() = q.x;
    q_.y() = q.y;
    q_.z() = q.z;
    Matrix3d R_init = q_.toRotationMatrix();
    this->ahrs_R_init = R_init;
    //TODO:set R_init into graph state.
    this->currentState.R() = R_init;
    return true;
}
bool GlobalOptimizationGraph::init_SLAM(//const geometry_msgs::PoseStamped& slam_msg
)
{
    //match rotation matrix and translation vector to E,0.
    auto slam_msg = this->pSLAM_Buffer->getLastMessage();
    auto q = slam_msg.pose.orientation;//TODO
    Eigen::Quaterniond q_;
    q_.w() = q.w;
    q_.x() = q.x;
    q_.y() = q.y;
    q_.z() = q.z;
    auto t_slam = slam_msg.pose.position;
    Vector3d t_slam_(t_slam.x,t_slam.y,t_slam.z);
    //Matrix3d flu_to_enu1,flu_to_enu2;
    //flu_to_enu1 << 0,1,0,-1,0,0,0,0,1;
    //flu_to_enu2 << 1,0,0,0,0,1,0,-1,0;
    
    
    //SLAM_to_UAV_coordinate_transfer_R = (q_.toRotationMatrix()*flu_to_enu1*flu_to_enu2).inverse() *this->ahrs_R_init;
    Matrix3d prev_transform;
    //prev_transform<<1,0,0,0,-1,0,0,0,-1;
    //prev_transform<<1,0,0,0,1,0,0,0,-1;
    //prev_transform<<-1,0,0,0,1,0,0,0,1;
    SLAM_to_UAV_coordinate_transfer_R = (q_.toRotationMatrix()).inverse()* (this->ahrs_R_init);
    Matrix3d finaltransform;
    //finaltransform<<0,0,1,1,0,0,0,-1,0;
    
    
    //finaltransform<<0,0,1,-1,0,0,0,1,0;
    //finaltransform<<-1,0,0, 0,0,-1, 0,1,0;//rotate 90deg y.
    finaltransform<<0,0,-1,1,0,0,0,1,0;
    
    
    //finaltransform<<0,0,-1,-1,0,0,0,-1,0;
    SLAM_to_UAV_coordinate_transfer_R = SLAM_to_UAV_coordinate_transfer_R*finaltransform;
    
    
    SLAM_to_UAV_coordinate_transfer_t = -1 * SLAM_to_UAV_coordinate_transfer_R * t_slam_;
    
    int vinit_pr_id = this->iterateNewestVertexPRID();
    if(vinit_pr_id!=0)
    {
        cout<<"ERROR:vinit_pr_id!=0!"<<endl;
        return false;
    }
    /*
    this->currentState.R() = this->ahrs_R_init;
    this->currentState.t() = Vector3d(0,0,0);
    this->currentState.setId(vinit_pr_id);
    this->currentState.setFixed(true);
    */
    VertexPR* pcurrent_state = new VertexPR();
    pcurrent_state->R() = this->ahrs_R_init;
    pcurrent_state->t() = Vector3d(0,0,0);
    pcurrent_state->setId(vinit_pr_id);
    pcurrent_state->setFixed(true);
    this->optimizer.addVertex(pcurrent_state);
    cout<<"[SLAM_INFO] SLAM_init_R:\n"<<q_.toRotationMatrix()<<endl;
    //cout<<"[SLAM_INFO] flu_to_enu: \n"<<flu_to_enu1*flu_to_enu2<<endl;
    cout<<"[SLAM_INFO] ahrs_R:\n"<<this->ahrs_R_init<<endl;
    cout<<"[SLAM_INFO] SLAM_to_UAV_coordinate_transfer R:\n"<<SLAM_to_UAV_coordinate_transfer_R<<endl;
    cout<<"[SLAM_INFO] slam init finished!!!"<<endl;
    return true;
}

bool GlobalOptimizationGraph::init_gps()//init longitude,latitude,altitude.
    //TODO:Init a GPS callback buffer block class,inherit callback buffer base,implement init and check avail.
{
    cout<<"In GOG::init_gps(): trying to init gps."<<endl;
    double avg_lon,avg_lat,avg_alt;
	avg_lon = 0;
	avg_lat = 0;
	avg_alt = 0;
	int count = 0;
	//assert gaussian distribution,sigma>3 is rejection region.
	double vari_lon,vari_lat,vari_alt;
	vari_lon=0;
	vari_lat=0;
	vari_alt=0;
    
    //for(auto g:*(this->pGPS_Buffer))
    auto buf = this->pGPS_Buffer->getCopyVec();
    for(auto g: buf)
    {
        if(g.status.status>=g.status.STATUS_FIX) //which means gps available.
        {
            avg_lon+=g.longitude;
            avg_lat+=g.latitude;
            avg_alt+=g.altitude;
            count++;
            
        }
        else
        {
            cout<<"MSG status:"<<(int)g.status.status<<" is smaller than g.status.STATUS_FIX."<<endl;
            cout<<"info: lon lat alt:"<<g.longitude<<","<<g.latitude<<","<<g.altitude<<endl;
            
            //TODO:remove this.DEBUG ONLY!!!
            cout<<"WARNING: NO CHECK ON gps status!"<<endl;
            avg_lon+=g.longitude;
            avg_lat+=g.latitude;
            avg_alt+=g.altitude;
            count++;
        }
    }
	if(count<GPS_AVAIL_MINIMUM)
	{
        cout<<"In GOG::init_gps(): count < GPS_AVAIL_MINIMUM.count:"<<count<<"."<<endl;
	    return false;
	}
	avg_lon = avg_lon/count;
	avg_lat = avg_lat/count;
	avg_alt = avg_alt/count;
	for(auto g:buf)
	{
	    //if(g.status.status>=g.status.STATUS_FIX)
	    if(true)//TODO:debug only!
        {
	        vari_lon+=pow(g.longitude-avg_lon,2);
			vari_lat+=pow(g.latitude-avg_lat,2);
			vari_alt+=pow(g.altitude-avg_alt,2);
	    }
	}
	vari_lon/=count;
	vari_lat/=count;
	vari_alt/=count;
    vari_lon = sqrt(vari_lon);
    vari_lat = sqrt(vari_lat);
    vari_alt = sqrt(vari_alt);
	cout<<"[GPS_INFO]GPS Initiated at LONGITUDE:"<<avg_lon<<",LATITUDE:"<<avg_lat<<",ALTITUDE:"<<avg_alt<<".VARIANCE:"<<vari_lon<<", "<<vari_lat<<", "<<vari_alt<<"."<<endl;
	cout<<"Available count:"<<count<<"."<<endl;
	
	//expand at avg lon,lat.
	GPSExpand GE;
	GE.expandAt(avg_lon,avg_lat,avg_alt);
    this->GPS_coord.expandAt(avg_lon,avg_lat,avg_alt);
    double lon_variance_m,lat_variance_m;
    lon_variance_m = GE.vari_km_per_lon_deg()*vari_lon*1000;
    lat_variance_m = GE.vari_km_per_lat_deg()*vari_lat*1000;

	cout<<"[GPS_INFO] X variance:"<<GE.vari_km_per_lon_deg()*vari_lon*1000<<"m;Y variance:"
            <<GE.vari_km_per_lat_deg()*vari_lat*1000<<
            "m,ALTITUDE variance:"<<vari_alt<<"m."<<endl;
    //check variance:
    double gps_init_variance_thres = (this->fSettings)["GPS_INIT_VARIANCE_THRESHOLD_m"];
    double gps_init_alt_variance_thres =  (this->fSettings)["GPS_INIT_ALT_VARIANCE_THRESHOLD_m"];
    cout<<"Config file read."<<endl;
    if(lon_variance_m> gps_init_variance_thres || lat_variance_m > gps_init_variance_thres
        || vari_alt>gps_init_alt_variance_thres)
    {
        cout<<"WARNING:GPS init failed.VARIANCE out of threshold."<<endl;
        cout<<"THRESHOLD(m):"<<gps_init_variance_thres<<endl;
        return false;
    }
    else
    {
        cout<<"gps variance check passed!"<<endl;
    }
    this->gps_init_longitude = avg_lon;
    this->gps_init_latitude = avg_lat;
    this->gps_init_altitude = avg_alt;

    this->gps_init_lon_variance = vari_lon;
    this->gps_init_lat_variance = vari_lat;
    this->gps_init_alt_variance = vari_alt;

    gps_init_success = true;
    if ( !(this->status&1)) // gps_avail 
    {
        this->status |= 1;//set status.
    }
	return true;
}
void GlobalOptimizationGraph::addBlockAHRS(const nav_msgs::Odometry& AHRS_msg)
{
    cout<<"Adding AHRS block to Optimization Graph!"<<endl;
    EdgeAttitude* pEdgeAttitude = new EdgeAttitude();
    auto q = AHRS_msg.pose.pose.orientation;//TODO
    Eigen::Quaterniond q_;
    q_.w() = q.w;
    q_.x() = q.x;
    q_.y() = q.y;
    q_.z() = q.z;
    //pEdgeAttitude->setMeasurement(q_);//TODO
    Eigen::Matrix<double,3,3> info_mat = Eigen::Matrix<double,3,3>::Identity();
    pEdgeAttitude->setInformation(info_mat);
    pEdgeAttitude->setLevel(!checkAHRSValid());
    cout<<"adding vertex ahrs."<<endl;
    
    //pEdgeAttitude->setVertex(0, dynamic_cast<g2o::OptimizableGraph::Vertex *>(this->pCurrentPR.get()));
    int newest_vpr_id = this->newestVertexPR_id;
    if(this->optimizer.vertex(newest_vpr_id) == NULL)
    {
        cout<<"error:(in addBlockAHRS() ) optimizer.vertex("<<newest_vpr_id<<") is NULL!"<<endl;
    }
    pEdgeAttitude->setVertex(0, dynamic_cast<g2o::OptimizableGraph::Vertex *> (this->optimizer.vertex(newest_vpr_id)) );
    cout<<"added vertex ahrs."<<endl;
    this->optimizer.addEdge(pEdgeAttitude);
    cout<<"AHRS EDGE ADDED!"<<endl;
}
void GlobalOptimizationGraph::addBlockGPS(const sensor_msgs::NavSatFix& GPS_msg)
{
    cout <<"Adding GPS block to Optimization Graph!"<<endl;
    
    double gps_max_delay_sec = this->fSettings["GPS_MAX_DELAY_SEC"];
    double delay_sec = this->pGPS_Buffer->queryLastMessageTime() - GPS_msg.header.stamp.toSec();
    if(this->pGPS_Buffer->queryLastMessageTime() - GPS_msg.header.stamp.toSec() > gps_max_delay_sec)
    {
        cout<<"[INFO]GPS msg time out.Delay:"<<delay_sec<<"s."<<endl;
        
        //set gps invalid. if possible,try reinit.
        this->status&=(0x02);
        //this->gps_init_success = false;
    }

    if(this->allow_gps_usage == false || this->gps_init_success == false||  !(this->status&0x01)  )//check if GPS valid.
    {
        cout<<"[WARNING] Unable to add GPS edge.GPS usage is forbidden in config file."<<endl;
        if(this->allow_gps_usage)
        {
            cout<<"trying to reinit gps:"<<endl;
            this->try_reinit_gps();
        }
        return;
    }
    /*//state shall be put into state tranfer module.
    if(!(this->status&this->STATUS_WITH_GPS_NO_SCENE))
    {
        //match with new coordinate
        this->GPS_coord.init_at(xxx_msg);
    }*/


  
    EdgePRGPS* pEdgePRGPS = new EdgePRGPS();
    double delta_lon = GPS_msg.longitude - GPS_coord.getLon();
    double delta_lat = GPS_msg.latitude - GPS_coord.getLat();
    double delta_alt = GPS_msg.altitude - GPS_coord.getAlt();
    cout <<"setting gps measurement!"<<endl;

    //since the quaternion is NED defined,we do not need any rotation here.
    pEdgePRGPS->setMeasurement( Vector3d(delta_lon*1000*GPS_coord.vari_km_per_lon_deg(),
                                delta_lat*1000*GPS_coord.vari_km_per_lat_deg(),
                                delta_alt)
                            );

    double info_lon,info_lat,info_alt;
    double gps_min_variance_lonlat_m = (this->fSettings)["GPS_MIN_VARIANCE_LONLAT_m"];
    double gps_min_variance_alt_m = (this->fSettings)["GPS_MIN_VARIANCE_ALT_m"];
    cout <<"in addGPSBlock(): calc info mat!"<<endl;
  info_lon = min((1.0/this->gps_init_lon_variance),1.0/gps_min_variance_lonlat_m);
  info_lat = min((1.0/this->gps_init_lat_variance),1.0/gps_min_variance_lonlat_m);
  info_alt = min((1.0/this->gps_init_alt_variance),1.0/gps_min_variance_alt_m);

  Eigen::Matrix<double,3,3> info_mat = Eigen::Matrix<double,3,3>::Identity();
  info_mat(0,0) = info_lon;
  info_mat(1,1) = info_lat;
  info_mat(2,2) = info_alt;
  pEdgePRGPS->setInformation(info_mat);//the inverse mat of covariance.
  int gps_valid = (GPS_msg.status.status >= GPS_msg.status.STATUS_FIX?1:0);
  pEdgePRGPS->setLevel(gps_valid);
  cout<<"adding edge gps!"<<endl;
  cout<<"[GPS_INFO]GPS_relative_pos:"<<
            delta_lon*1000*GPS_coord.vari_km_per_lon_deg()<<","<<
            delta_lat*1000*GPS_coord.vari_km_per_lat_deg()<<","<<
            delta_alt<<endl;
  
  int newest_vpr_id = this->newestVertexPR_id;
  if(this->optimizer.vertex(newest_vpr_id) == NULL)
  {
      cout<<"error:(in addBlockGPS() ) optimizer.vertex("<<newest_vpr_id<<") is NULL!"<<endl;
  }
  pEdgePRGPS->setVertex(0,dynamic_cast<g2o::OptimizableGraph::Vertex *>(this->optimizer.vertex(newest_vpr_id)));

  this->optimizer.addEdge(pEdgePRGPS);
}

bool GlobalOptimizationGraph::estimateCurrentSpeed()
{
  //step<1> check status change log;
  //step<2> if status not changed;calc current speed.
  //step<3> set into vector.
}


void GlobalOptimizationGraph::addBlockSLAM(const geometry_msgs::PoseStamped& SLAM_msg)
//if use this api,only reserve the last one.
//void GlobalOptimizationGraph::addBlockSLAM(std::vector<const geometry_msgs::PoseStamped&> SLAM_msg_list)
//for multiple msgs.
{
    //part<1> Rotation.
    cout<<"addBlockSLAM() : part 1."<<endl;
    auto pEdgeSlam = new EdgeAttitude();
    //shared_ptr<g2o::OptimizableGraph::Edge *> ptr_slam(pEdgeSlam);
    //pEdgeSlam->setId(this->EdgeVec.size());//TODO
    //this->EdgeVec.push_back(ptr_slam);
    int newest_vpr_id = this->newestVertexPR_id;
    
    if(this->optimizer.vertex(newest_vpr_id) == NULL)
    {
        cout<<"error:(in addBlockSLAM() ) optimizer.vertex(0) is NULL!"<<endl;
    }
    pEdgeSlam->setVertex(0,dynamic_cast<g2o::OptimizableGraph::Vertex *>(this->optimizer.vertex(newest_vpr_id)));
    auto q = SLAM_msg.pose.orientation;
    
    auto p = SLAM_msg.pose.position;
    
    
    
    cout<<"addBlockSLAM():setting quaternion"<<endl;
    Eigen::Quaterniond q_;
    q_.w() = q.w;
    q_.x() = q.x;
    q_.y() = q.y;
    q_.z() = q.z;
    auto R_ = q_.toRotationMatrix();
    R_ = R_* this->SLAM_to_UAV_coordinate_transfer_R;
    Vector3d t_(p.x,p.y,p.z);
    cout<<"TODO:fix +t!!!"<<endl;
    t_ = this->SLAM_to_UAV_coordinate_transfer_R*t_ ;//+ this->SLAM_to_UAV_coordinate_transfer_t;
    cout <<"[SLAM_INFO] slam position:"<<t_[0]<<","<<t_[1]<<","<<t_[2]<<endl;
    Eigen::Matrix<double,3,3> info_mat_slam_rotation = Eigen::Matrix<double,3,3>::Identity();
    cout<<"addBlockSLAM():part 3"<<endl;
    
    //VertexPR slam_pr_R_only;
    
    //slam_pr_R_only.R() = q_.toRotationMatrix().topLeftCorner(3,3);
    auto se3_slam = SO3d::log(q_.toRotationMatrix() );
    pEdgeSlam->setMeasurement(se3_slam);
    pEdgeSlam->setInformation(info_mat_slam_rotation);
    cout <<"Measurement,information mat set.Adding edge slam!!!"<<endl;
    
    this->optimizer.addEdge(pEdgeSlam);
    cout<<"Edge SLAM added successfully."<<endl;
    //part<2> Translation
    this->autoInsertSpeedVertexAfterInsertCurrentVertexPR();
    //Edge PRV.
    /*//TODO:fill Edge SLAM PRV.
    if(this->historyStatesWindow.size()>0)
    {
        shared_ptr<Edge_SLAM_PRV>pEdge_SLAM_PRV(new Edge_SLAM_PRV());
        pEdge_SLAM_PRV->setId(this->EdgeVec.size());
        this->EdgeVec.push_back(pEdge_SLAM_PRV);
        //TODO:fit in type.
        pEdge_SLAM_PRV->setVertex(0,dynamic_cast<g2o::OptimizableGraph::Vertex *>&(this->historyStatesWindow[this->historyStatesWindow.size()-1]));//old one.
        pEdge_SLAM_PRV->setVertex(1,dynamic_cast<g2o::OptimizableGraph::Vertex *>(this->pCurrentPR.get()));
        pEdge_SLAM_PRV->setVertex(2,dynamic_cast<g2o::OptimizableGraph::Vertex *> (&(this->historySpeed[this->historySpeed.size()-1])));
        pEdge_SLAM_PRV->setVertex(3,dynamic_cast<g2o::OptimizableGraph::Vertex *> (&this->currentSpeed));
        g2o::OptimizableGraph::Vertex::
        //pEdge_SLAM_PRV->setInformation(....);//TODO
    }
    else
    {//just add translation.
        shared_ptr<EdgePRGPS> pEdgePositionSLAM(new EdgePRGPS());
        pEdgePositionSLAM->setVertex(0,dynamic_cast<g2o::OptimizableGraph::Vertex *>(this->pCurrentPR.get());
        //calc infomation mat from multiple/single slam msg.May be it can be estimated from points num or quality.
        //pEdgePositionSLAM->setInformation(...);TODO
    }*/


    //TODO
}
void GlobalOptimizationGraph::addSLAM_edgeprv(const geometry_msgs::PoseStamped& SLAM_msg)
{
    //step<1>.Form a IMU_Preintegration like object so we can form edgeprv.
    //TODO:set DV.
    ygz::IMUPreIntegration preintv;
    //preintv.setDP(SLAM_msg.pose-init_pose....);
    //preintv.setDV();
    /*
    ygz::EdgeSLAMPRV * pPRV = new EdgeSLAMPRV();
    pPRV->setVertex(0,....);//vertex pr1
    pPRV->setVertex(1,....);//vertex pr2
    pPRV->setVertex(...)//vertex speed1
    pPRV->setVertex(...)//vertex speed2
    */
}
void GlobalOptimizationGraph::addBlockQRCode()
{
    //pEdgeQRCode = 
}
void addBlockSceneRetriever_StrongCoupling(); //Solve se(3) from multiple points PRXYZ;
void addBlockSceneRetriever_WeakCoupling();//just do square dist calc.

void GlobalOptimizationGraph::addBlockSceneRetriever()
{
    ;//TODO
    //step<1>.add vertex PR for scene.
    //set infomation matrix that optimize Rotation and altitude.Do not change longitude and latitude.
    //pBlockSceneRetriever = 
}
bool GlobalOptimizationGraph::tryInitVelocity()
{
    cout<<"Nothing to do now!!!"<<endl;
    return false;//TODO :fill in this.
}
bool GlobalOptimizationGraph::checkAHRSValid()
{
    cout<<"Nothing to do now!!!"<<endl;
    return false;//TODO: fill in this.
}

#endif




