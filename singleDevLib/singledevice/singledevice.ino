/*
 * 该例程配合singleDevLib库使用。
 * 需先在恰恰云平台配置网关及子设备。平台网址：http://182.92.218.115:8000/
 * 建议使用谷歌浏览器。
 */

#include <SPI.h>
#include <WiFi.h>
#include <PubSubClient.h>
#include <QiaqiaMqttSingleDevice.h>


unsigned long SENDConfigStart = 0;
unsigned int SENDFirstTimeState = 1;
QiaqiaMQTTSD qiaqia;

void readCallback(int offsetindex, char *devack) {
  Serial.println("i am here 222");
  char *p;
  char one[] = {"3.7"};
  char two[] = {"23"};
  char three[] = {"1"};
  switch(offsetindex){
    case 1:
      p = one;
     break;
    case 2:
      p = two;
      break;
    case 3:
      p = three;
      break;
    default:
      break;
  }
  qiaqia.sdAck(devack, p);
}

void writeCallback(int offsetindex, float value) { 
    Serial.println("enter write callback!");
    Serial.println(offsetindex);
    Serial.println(value);

}

void setup()
{
  Serial.begin(115200);

  qiaqia.sdInit();
  //Activate read callback function
  qiaqia.setReadCallback(readCallback);
  //Activate write callback function
  qiaqia.setWriteCallback(writeCallback);
}

void loop()
{
    
    qiaqia.sdRun();
    delay(50);

    
    if(SENDFirstTimeState == 1){
        SENDConfigStart = millis();
        SENDFirstTimeState = 0;
    }   
    if((millis() - SENDConfigStart) < 0){
        Serial.println("millis() runoff");  
        SENDFirstTimeState = 1;  
    }
    if((millis() - SENDConfigStart) > 3000){ 
        qiaqia.sdValueChange(1, "1.6");
        qiaqia.sdValueChange(2, "2.6");
        qiaqia.sdValueChange(3, "3.6");

        SENDFirstTimeState = 1;
        SENDConfigStart = 0;
    }
         
          
}





