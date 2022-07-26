h5gg.require(7.8); //设定最低需求的H5GG版本号

//将插件dylib放到.app目录中调用
var MyAlert = h5gg.loadPlugin("MyAlert", "customAlert.dylib");

if(!MyAlert) throw "插件加载失败";

//延迟一秒等H5GG的脚本执行完毕弹出之后再执行调用, 避免后面的弹框被阻塞弹不出来
setTimeout(function(){
    
    MyAlert.choice(["弹框1","弹框2","弹框3"], function(item)
    {
        if(item=='弹框1') MyAlert.alert0(); //调用无参数函数

        if(item=='弹框2')MyAlert.alert1("提示文本"); //调用一个参数函数
        
        if(item=='弹框3') MyAlert.alert2("标题文本", "内容文本"); //调用两个参数函数
    });
    
}, 1000);
