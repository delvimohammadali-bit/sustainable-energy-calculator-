#define _POSIX_C_SOURCE 200809L
#include <arpa/inet.h>
#include <ctype.h>
#include <math.h>
#include <signal.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <unistd.h>

#define PORT 8080
#define MAX_APPLIANCES 100
#define MAX_BILLS 120
#define REQUEST_CAP 100000
#define RESPONSE_CAP 160000
#define FIELD_CAP 128

typedef struct { char name[FIELD_CAP], room[FIELD_CAP]; double watts, hours; int quantity; } Appliance;
typedef struct { int month, year; double units, amount; } Bill;
typedef struct { char data[RESPONSE_CAP]; size_t len; } Buffer;

static Appliance appliances[MAX_APPLIANCES];
static Bill bills[MAX_BILLS];
static int appliance_count, bill_count;
static char request_body[REQUEST_CAP];

static char *find_header_ci(char *haystack, const char *needle)
{
    size_t n = strlen(needle);
    for (char *p = haystack; *p; ++p) {
        size_t i = 0;
        while (i < n && p[i] && tolower((unsigned char)p[i]) == tolower((unsigned char)needle[i])) ++i;
        if (i == n) return p;
    }
    return NULL;
}

static void addf(Buffer *b, const char *fmt, ...)
{
    if (b->len >= sizeof b->data - 1) return;
    va_list ap; va_start(ap, fmt);
    int n = vsnprintf(b->data + b->len, sizeof b->data - b->len, fmt, ap);
    va_end(ap);
    if (n > 0) b->len += (size_t)n < sizeof b->data - b->len ? (size_t)n : sizeof b->data - b->len - 1;
}

static void json_string(Buffer *b, const char *s)
{
    addf(b, "\"");
    for (const unsigned char *p = (const unsigned char *)s; *p && b->len < sizeof b->data - 8; ++p) {
        if (*p == '"' || *p == '\\') addf(b, "\\%c", *p);
        else if (*p == '\n') addf(b, "\\n");
        else if (*p == '\r') addf(b, "\\r");
        else if (*p < 32) addf(b, " ");
        else addf(b, "%c", *p);
    }
    addf(b, "\"");
}

static void url_decode(const char *src, char *dst, size_t cap)
{
    size_t j = 0;
    for (size_t i = 0; src[i] && j + 1 < cap; ++i) {
        if (src[i] == '+') dst[j++] = ' ';
        else if (src[i] == '%' && isxdigit((unsigned char)src[i+1]) && isxdigit((unsigned char)src[i+2])) {
            char hex[3] = {src[i+1], src[i+2], 0}; dst[j++] = (char)strtol(hex, NULL, 16); i += 2;
        } else dst[j++] = src[i];
    }
    dst[j] = 0;
}

static void param(const char *key, char *out, size_t cap)
{
    out[0] = 0;
    char body[REQUEST_CAP];
    snprintf(body, sizeof body, "%s", request_body);
    char *save = NULL;
    for (char *part = strtok_r(body, "&", &save); part; part = strtok_r(NULL, "&", &save)) {
        char *eq = strchr(part, '='); if (!eq) continue;
        *eq = 0;
        char decoded_key[256]; url_decode(part, decoded_key, sizeof decoded_key);
        if (strcmp(decoded_key, key) == 0) { url_decode(eq + 1, out, cap); return; }
    }
}

static double pdouble(const char *key, double fallback)
{
    char s[128], *end; param(key, s, sizeof s);
    if (!*s) return fallback;
    double v = strtod(s, &end);
    return end != s && !*end && isfinite(v) ? v : fallback;
}

static int pint(const char *key, int fallback)
{
    char s[64], *end; param(key, s, sizeof s);
    if (!*s) return fallback;
    long v = strtol(s, &end, 10);
    return end != s && !*end && v >= -100000 && v <= 100000 ? (int)v : fallback;
}

static double appliance_kwh(const Appliance *a)
{ return a->watts * a->hours * a->quantity * 30.0 / 1000.0; }

static double total_kwh(void)
{
    double n = 0; for (int i = 0; i < appliance_count; ++i) n += appliance_kwh(&appliances[i]); return n;
}

static double bill_cost(double units, double l1, double l2, double r1, double r2, double r3, double fixed)
{
    double cost = units <= l1 ? units * r1 : units <= l2 ? l1*r1 + (units-l1)*r2 : l1*r1 + (l2-l1)*r2 + (units-l2)*r3;
    return cost + fixed;
}

static const char *month_name(int m)
{
    static const char *names[] = {"", "January", "February", "March", "April", "May", "June", "July", "August", "September", "October", "November", "December"};
    return m >= 1 && m <= 12 ? names[m] : "Unknown";
}

static int find_bill(int month, int year)
{ for (int i = 0; i < bill_count; ++i) if (bills[i].month == month && bills[i].year == year) return i; return -1; }

static void save_data(void)
{
    mkdir("data", 0755);
    FILE *f = fopen("data/appliances.csv", "w");
    if (f) { for (int i=0;i<appliance_count;i++) fprintf(f,"%s,%s,%.3f,%.3f,%d\n",appliances[i].name,appliances[i].room,appliances[i].watts,appliances[i].hours,appliances[i].quantity); fclose(f); }
    f = fopen("data/monthly_history.csv", "w");
    if (f) { for (int i=0;i<bill_count;i++) fprintf(f,"%d,%d,%.3f,%.2f\n",bills[i].month,bills[i].year,bills[i].units,bills[i].amount); fclose(f); }
}

static void load_data(void)
{
    mkdir("data", 0755);
    FILE *f = fopen("data/appliances.csv", "r");
    if (f) { while (appliance_count < MAX_APPLIANCES && fscanf(f,"%127[^,],%127[^,],%lf,%lf,%d\n",appliances[appliance_count].name,appliances[appliance_count].room,&appliances[appliance_count].watts,&appliances[appliance_count].hours,&appliances[appliance_count].quantity)==5) appliance_count++; fclose(f); }
    f = fopen("data/monthly_history.csv", "r");
    if (f) { while (bill_count < MAX_BILLS && fscanf(f,"%d,%d,%lf,%lf\n",&bills[bill_count].month,&bills[bill_count].year,&bills[bill_count].units,&bills[bill_count].amount)==4) bill_count++; fclose(f); }
    if (!appliance_count) {
        Appliance demo[] = {{"LED bulbs","Living room",9,6,4},{"Ceiling fan","Bedroom",75,10,2},{"Refrigerator","Kitchen",180,10,1},{"Laptop","Study",65,6,1},{"Television","Living room",110,4,1}};
        memcpy(appliances, demo, sizeof demo); appliance_count = (int)(sizeof demo / sizeof demo[0]);
    }
}

static void response(int fd, const char *status, const char *type, const char *body, size_t len)
{
    char h[512]; int n = snprintf(h,sizeof h,"HTTP/1.1 %s\r\nContent-Type: %s\r\nContent-Length: %zu\r\nCache-Control: no-store\r\nConnection: close\r\nAccess-Control-Allow-Origin: *\r\n\r\n",status,type,len);
    send(fd,h,(size_t)n,0); size_t sent=0;
    while(sent<len) { ssize_t k=send(fd,body+sent,len-sent,0); if(k<=0) break; sent+=(size_t)k; }
}

static void json_response(int fd, Buffer *b)
{ response(fd,"200 OK","application/json; charset=utf-8",b->data,b->len); }

static int valid_common(double watts, double hours, int qty)
{ return isfinite(watts) && isfinite(hours) && watts > 0 && watts <= 100000 && hours > 0 && hours <= 24 && qty >= 1 && qty <= 100; }

static void api_state(int fd)
{
    Buffer b={0}; addf(&b,"{\"appliances\":[");
    for(int i=0;i<appliance_count;i++){ Appliance *a=&appliances[i]; if(i)addf(&b,","); addf(&b,"{\"id\":%d,\"name\":",i+1);json_string(&b,a->name);addf(&b,",\"room\":");json_string(&b,a->room);addf(&b,",\"watts\":%.2f,\"hours\":%.2f,\"quantity\":%d,\"kwh\":%.2f}",a->watts,a->hours,a->quantity,appliance_kwh(a)); }
    addf(&b,"],\"bills\":[");
    for(int i=0;i<bill_count;i++){Bill *x=&bills[i];if(i)addf(&b,",");addf(&b,"{\"id\":%d,\"month\":%d,\"year\":%d,\"units\":%.2f,\"amount\":%.2f}",i+1,x->month,x->year,x->units,x->amount);}
    addf(&b,"],\"totalKwh\":%.2f}",total_kwh()); json_response(fd,&b);
}

static void api_appliance(int fd)
{
    char action[32],name[FIELD_CAP],room[FIELD_CAP];param("action",action,sizeof action);param("name",name,sizeof name);param("room",room,sizeof room);
    int id=pint("id",0), qty=pint("quantity",1); double watts=pdouble("watts",0),hours=pdouble("hours",0); Buffer b={0};
    if(!strcmp(action,"delete")){ if(id<1||id>appliance_count){addf(&b,"{\"ok\":false,\"message\":\"Select a valid appliance ID.\"}");} else {for(int i=id-1;i<appliance_count-1;i++)appliances[i]=appliances[i+1];appliance_count--;save_data();addf(&b,"{\"ok\":true,\"message\":\"Appliance deleted.\"}");}}
    else if(!*name||!*room||strchr(name,',')||strchr(room,',')||!valid_common(watts,hours,qty)){addf(&b,"{\"ok\":false,\"message\":\"Enter a name, room, valid wattage, hours (up to 24), and quantity. Commas are not allowed in names.\"}");}
    else if(!strcmp(action,"add")){if(appliance_count>=MAX_APPLIANCES)addf(&b,"{\"ok\":false,\"message\":\"The appliance limit is 100.\"}");else{Appliance *a=&appliances[appliance_count++];snprintf(a->name,sizeof a->name,"%s",name);snprintf(a->room,sizeof a->room,"%s",room);a->watts=watts;a->hours=hours;a->quantity=qty;save_data();addf(&b,"{\"ok\":true,\"message\":\"Appliance added.\"}");}}
    else if(!strcmp(action,"edit")){if(id<1||id>appliance_count)addf(&b,"{\"ok\":false,\"message\":\"Select a valid appliance ID.\"}");else{Appliance *a=&appliances[id-1];snprintf(a->name,sizeof a->name,"%s",name);snprintf(a->room,sizeof a->room,"%s",room);a->watts=watts;a->hours=hours;a->quantity=qty;save_data();addf(&b,"{\"ok\":true,\"message\":\"Appliance updated.\"}");}}
    else addf(&b,"{\"ok\":false,\"message\":\"Unknown appliance action.\"}"); json_response(fd,&b);
}

static void api_history(int fd)
{
    char action[32];param("action",action,sizeof action);int id=pint("id",0),month=pint("month",0),year=pint("year",0);double units=pdouble("units",-1),amount=pdouble("amount",-1);Buffer b={0};
    if(!strcmp(action,"delete")){if(id<1||id>bill_count)addf(&b,"{\"ok\":false,\"message\":\"Select a valid bill record.\"}");else{for(int i=id-1;i<bill_count-1;i++)bills[i]=bills[i+1];bill_count--;save_data();addf(&b,"{\"ok\":true,\"message\":\"Bill record deleted.\"}");}}
    else if(month<1||month>12||year<2000||year>2100||units<0||units>100000||amount<0||amount>10000000)addf(&b,"{\"ok\":false,\"message\":\"Enter a valid month, year, units and bill amount.\"}");
    else{int i=find_bill(month,year);if(i<0){if(bill_count>=MAX_BILLS){addf(&b,"{\"ok\":false,\"message\":\"History limit reached.\"}");json_response(fd,&b);return;}i=bill_count++;}bills[i]=(Bill){month,year,units,amount};save_data();addf(&b,"{\"ok\":true,\"message\":\"Monthly bill saved.\"}");}json_response(fd,&b);
}

static void read_tariff(double *l1,double *l2,double *r1,double *r2,double *r3,double *fixed,double *budget)
{
    *l1=pdouble("limit1",100);*l2=pdouble("limit2",200);*r1=pdouble("rate1",4.5);*r2=pdouble("rate2",7);*r3=pdouble("rate3",8.5);*fixed=pdouble("fixed",100);*budget=pdouble("budget",2000);
    if(*l1<0)*l1=100;if(*l2<=*l1)*l2=*l1+100;if(*r1<0)*r1=0;if(*r2<0)*r2=0;if(*r3<0)*r3=0;if(*fixed<0)*fixed=0;
}

static void api_calculate(int fd)
{
    int month=pint("month",1),year=pint("year",2026);double l1,l2,r1,r2,r3,fixed,budget;read_tariff(&l1,&l2,&r1,&r2,&r3,&fixed,&budget);double units=total_kwh(),cost=bill_cost(units,l1,l2,r1,r2,r3,fixed);int i=find_bill(month,year);Buffer b={0};
    addf(&b,"{\"units\":%.2f,\"amount\":%.2f,\"budget\":%.2f,\"withinBudget\":%s,\"historyFound\":%s,\"historyUnits\":%.2f,\"historyAmount\":%.2f,\"difference\":%.2f}",units,cost,budget,cost<=budget?"true":"false",i>=0?"true":"false",i>=0?bills[i].units:0,i>=0?bills[i].amount:0,i>=0?cost-bills[i].amount:0);json_response(fd,&b);
}

static void api_simulate(int fd)
{
    int id=pint("id",0);double new_w=pdouble("watts",0),new_h=pdouble("hours",0),l1,l2,r1,r2,r3,fixed,budget;read_tariff(&l1,&l2,&r1,&r2,&r3,&fixed,&budget);Buffer b={0};
    if(id<1||id>appliance_count||new_w<=0||new_w>100000||new_h<=0||new_h>24)addf(&b,"{\"ok\":false,\"message\":\"Select an appliance and enter valid new wattage and hours.\"}");
    else{Appliance *a=&appliances[id-1];double before=total_kwh(),after=before-appliance_kwh(a)+new_w*new_h*a->quantity*30.0/1000.0;double old_cost=bill_cost(before,l1,l2,r1,r2,r3,fixed),new_cost=bill_cost(after,l1,l2,r1,r2,r3,fixed);addf(&b,"{\"ok\":true,\"beforeKwh\":%.2f,\"afterKwh\":%.2f,\"savedKwh\":%.2f,\"beforeCost\":%.2f,\"afterCost\":%.2f,\"savedCost\":%.2f}",before,after,before-after,old_cost,new_cost,old_cost-new_cost);}json_response(fd,&b);
}

static void api_peak(int fd)
{
    char ids[2048];param("ids",ids,sizeof ids);double limit=pdouble("limit",0),peak=0;char *save=NULL;int valid=0;
    for(char *p=strtok_r(ids,",",&save);p;p=strtok_r(NULL,",",&save)){char *end;long id=strtol(p,&end,10);if(end==p||*end||id<1||id>appliance_count)continue;peak+=appliances[id-1].watts*appliances[id-1].quantity/1000.0;valid++;}
    Buffer b={0};if(!valid)addf(&b,"{\"ok\":false,\"message\":\"Enter one or more valid appliance IDs separated by commas.\"}");else addf(&b,"{\"ok\":true,\"peakKw\":%.3f,\"limitKw\":%.3f,\"overLimit\":%s,\"count\":%d}",peak,limit,limit>0&&peak>limit?"true":"false",valid);json_response(fd,&b);
}

static void api_report(int fd)
{
    int month=pint("month",0),year=pint("year",0);double l1,l2,r1,r2,r3,fixed,budget;read_tariff(&l1,&l2,&r1,&r2,&r3,&fixed,&budget);double units=total_kwh(),cost=bill_cost(units,l1,l2,r1,r2,r3,fixed);int bi=find_bill(month,year),top=-1;
    for(int i=0;i<appliance_count;i++)if(top<0||appliance_kwh(&appliances[i])>appliance_kwh(&appliances[top]))top=i;
    if(month<1||month>12||year<2000||year>2100){Buffer e={0};addf(&e,"{\"ok\":false,\"message\":\"Enter a valid report month and year.\"}");json_response(fd,&e);return;}
    char report[50000];int n=snprintf(report,sizeof report,"SMART ENERGY MONTHLY REPORT\n%s %d\n\nPROJECT: Smart Energy Consumption and Electricity Bill Calculator\nSDG ALIGNMENT: United Nations Sustainable Development Goal 7 - Affordable and Clean Energy\n\n1. MONTHLY ENERGY SUMMARY\nEstimated household energy: %.2f kWh\nEstimated bill from entered tariff slabs: Rs %.2f\nBudget: Rs %.2f (%s)\n",month_name(month),year,units,cost,budget,cost<=budget?"within budget":"over budget");
    n+=snprintf(report+n,sizeof report-(size_t)n,"\n2. APPLIANCE BREAKDOWN\nID  Appliance | Room | W | h/day | Qty | kWh/month\n");for(int i=0;i<appliance_count&&n<(int)sizeof report-300;i++)n+=snprintf(report+n,sizeof report-(size_t)n,"%d  %s | %s | %.1f | %.1f | %d | %.2f\n",i+1,appliances[i].name,appliances[i].room,appliances[i].watts,appliances[i].hours,appliances[i].quantity,appliance_kwh(&appliances[i]));
    n+=snprintf(report+n,sizeof report-(size_t)n,"\n3. PEAK ENERGY USER\n%s\n",top>=0?appliances[top].name:"No appliances recorded");
    if(bi>=0)n+=snprintf(report+n,sizeof report-(size_t)n,"\n4. MANUALLY ENTERED BILL COMPARISON\nRecorded bill: %.2f kWh, Rs %.2f\nEstimate difference: Rs %.2f\n",bills[bi].units,bills[bi].amount,cost-bills[bi].amount);else n+=snprintf(report+n,sizeof report-(size_t)n,"\n4. BILL HISTORY\nNo manual history is saved for %s %d.\n",month_name(month),year);
    n+=snprintf(report+n,sizeof report-(size_t)n,"\n5. CALCULATION METHOD AND LIMITATIONS\nEach appliance uses watts x hours/day x quantity x 30 days / 1000 to estimate monthly kWh. The bill estimate uses the editable progressive example slabs shown in the website. Actual BESCOM bills can include current tariff rules, taxes, subsidies, and other adjustments; this educational estimate is not an official bill. The user enters historical bills manually; the website does not access the BESCOM website.\n\n6. SDG 7 RELEVANCE\nThis project encourages awareness of household electricity use, helps users identify high-consumption appliances, and supports practical energy-saving decisions through estimates and what-if comparisons. Monitoring consumption can encourage efficient use and contribute to wider demand for affordable, reliable, sustainable and modern energy.\n\nPrepared by: ____________________   USN: ____________________\nCourse: C Programming   Department: ECE\n");
    mkdir("data",0755);mkdir("data/reports",0755);char path[128];snprintf(path,sizeof path,"data/reports/monthly_report_%04d_%02d.txt",year,month);FILE *f=fopen(path,"w");if(f){fwrite(report,1,(size_t)n,f);fclose(f);}Buffer b={0};addf(&b,"{\"ok\":true,\"filename\":");json_string(&b,path);addf(&b,",\"content\":");json_string(&b,report);addf(&b,"}");json_response(fd,&b);
}

static const char *mime_type(const char *p)
{ if(strstr(p,".css"))return "text/css; charset=utf-8";if(strstr(p,".js"))return "application/javascript; charset=utf-8";return "text/html; charset=utf-8"; }

static void handle(int fd)
{
    char req[REQUEST_CAP];size_t used=0;ssize_t got;while(used<sizeof req-1&&(got=recv(fd,req+used,sizeof req-1-used,0))>0){used+=(size_t)got;req[used]=0;char *headend=strstr(req,"\r\n\r\n");if(headend){size_t head=(size_t)(headend-req)+4;long want=0;char *cl=find_header_ci(req,"Content-Length:");if(cl)want=strtol(cl+15,NULL,10);if(used>=head+(size_t)(want>0?want:0))break;}}
    if(!used)return;req[used]=0;char method[8]={0},path[512]={0};sscanf(req,"%7s %511s",method,path);char *hend=strstr(req,"\r\n\r\n");request_body[0]=0;if(hend){size_t h=(size_t)(hend-req)+4;size_t bl=used>h?used-h:0;if(bl>=sizeof request_body)bl=sizeof request_body-1;memcpy(request_body,req+h,bl);request_body[bl]=0;}
    if(!strcmp(path,"/api/state")&&!strcmp(method,"GET")){api_state(fd);return;}
    if(!strcmp(method,"POST")){if(!strcmp(path,"/api/appliance")){api_appliance(fd);return;}if(!strcmp(path,"/api/history")){api_history(fd);return;}if(!strcmp(path,"/api/calculate")){api_calculate(fd);return;}if(!strcmp(path,"/api/simulate")){api_simulate(fd);return;}if(!strcmp(path,"/api/peak")){api_peak(fd);return;}if(!strcmp(path,"/api/report")){api_report(fd);return;}}
    const char *file=!strcmp(path,"/")?"index.html":!strcmp(path,"/style.css")?"style.css":!strcmp(path,"/app.js")?"app.js":NULL;
    if(file){FILE *f=fopen(file,"rb");if(f){static char page[100000];size_t n=fread(page,1,sizeof page,f);fclose(f);response(fd,"200 OK",mime_type(file),page,n);return;}}
    const char *msg="Not found";response(fd,"404 Not Found","text/plain; charset=utf-8",msg,strlen(msg));
}

int main(void)
{
    signal(SIGPIPE,SIG_IGN);load_data();int s=socket(AF_INET,SOCK_STREAM,0);if(s<0){perror("socket");return 1;}int yes=1;setsockopt(s,SOL_SOCKET,SO_REUSEADDR,&yes,sizeof yes);struct sockaddr_in addr={0};addr.sin_family=AF_INET;addr.sin_addr.s_addr=htonl(0x7f000001UL);addr.sin_port=htons(PORT);
    if(bind(s,(struct sockaddr*)&addr,sizeof addr)<0){perror("bind (is port 8080 already in use?)");close(s);return 1;}if(listen(s,16)<0){perror("listen");close(s);return 1;}
    printf("Smart Energy website is running at http://127.0.0.1:%d\nPress Ctrl+C to stop.\n",PORT);fflush(stdout);for(;;){int client=accept(s,NULL,NULL);if(client<0)continue;handle(client);close(client);}close(s);return 0;
}
