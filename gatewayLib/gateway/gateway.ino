/*
 * 该例程配合gatewayLib库使用。
 * 需先在恰恰云平台配置网关及子设备。平台网址：http://182.92.218.115:8000/
 * 建议使用谷歌浏览器。
 */

#include <SPI.h>
#include <WiFi.h>
#include <PubSubClient.h>
#include <QiaqiaMqttGateway.h>

unsigned long SENDConfigStart = 0;
unsigned int SENDFirstTimeState = 1;

unsigned long onlineConfigStart = 0;
unsigned int onlineFirstTimeState = 1;

uint8_t i = 20;
char _value[10];
    
qiaqiaMqttGateway qiaqia;

void readCallback(int busport, int devaddr, int offsetindex, char *devack) {
  Serial.println("enter read callback");
  qiaqia.gwAck(devack, "7.4");
}

void writeCallback(int busport, int devaddr, int offsetindex, float value) {
    Serial.println(busport);
    Serial.println(devaddr);
    Serial.println(offsetindex);
    
    Serial.println("enter write callback");
    Serial.println(value);

}

void timeCallback(int busport, int devaddr, char *datetime, char* devack){
  Serial.println(busport);
  Serial.println(devaddr);
  Serial.println("enter time callback");
  Serial.println(datetime);

  //用户填写子设备校时程序--写
  //根据budport和devaddr编写与子设备的通讯函数


  //用户填写子设备校时程序--读
  //根据budport和devaddr编写与子设备的通讯函数
  //qiaqia.gwAck(devack, "2016-12-21 14:51:23");
}

//通过devindex可以映射出子设备的busport和devaddr，
void restartCallback(int busport, int devaddr){
  Serial.println(busport);
  Serial.println(devaddr);

  if((busport == 0)&&(devaddr == 0)){
    Serial.println("enter gateway restart callback");
    delay(1000);
    PRCMSOCReset();  //Performs a software reset of a SOC
  }
  else {
    Serial.println("enter subdevice restart callback");
  }  //用户填写子设备复位程序，向子设备传输一个值，在子设备上映射出复位程序
}


void setup()
{
  Serial.begin(115200);
  qiaqia.gwInit();
  
  qiaqia.setReadCallback(readCallback);
  qiaqia.setWriteCallback(writeCallback);
  qiaqia.setGwAndDevTimeCallback(timeCallback);
  qiaqia.setGwAndDevRestartCallback(restartCallback);
}

void loop()
{
    qiaqia.gwRun();
    
         if(SENDFirstTimeState == 1){
              SENDConfigStart = millis();
              SENDFirstTimeState = 0;
         }   
         if((millis() - SENDConfigStart) < 0){
              Serial.println("millis() runoff"); 
              SENDFirstTimeState = 1;  
         }
         if((millis() - SENDConfigStart) > 6000){
            if(i<40){
              sprintf(_value, "%d.%d", i/10, i%10);
              i = i+2;
            }else{
              i = 20;
            }
           
            qiaqia.gwValueChange(2, 1, 1, _value);
            qiaqia.gwValueChange(2, 2, 1, _value);
            qiaqia.gwValueChange(2, 3, 1, _value);
            
            SENDFirstTimeState = 1;
            SENDConfigStart = 0;
         }
         
         
         if(onlineFirstTimeState == 1){
              onlineConfigStart = millis();
              onlineFirstTimeState = 0;
         }   
         if((millis() - onlineConfigStart) < 0){
              Serial.println("millis() runoff");  
              onlineFirstTimeState = 1; 
         }
         if((millis() - onlineConfigStart) > 3000){
       
            qiaqia.online(2, 1, true);
            qiaqia.online(2, 2, true);
            qiaqia.online(2, 3, true);
              
            onlineFirstTimeState = 1;
            onlineConfigStart = 0;
         }
}





