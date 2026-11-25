
The browser interface is made from HTML, CSS, and JavaScript. The C server does the main calculation and data-handling work.
How the parts communicate
You enter information on a website form
                  ↓
        JavaScript sends it to C
                  ↓
         server.c handles the request
          ↙                   ↘
  Calculates a result      Saves or loads data
          ↘                   ↙
           C sends back a response
                  ↓
      JavaScript updates the webpage
For example, when you add a fan, the browser sends the fan’s details to C. C checks and saves them, then sends a success message back.
1. Data types and storage
The program uses typedef struct to group related values:
- Appliance stores an appliance’s name, room, wattage, hours used per day, and quantity.
- Bill stores a bill’s month, year, energy units, and amount.
- Buffer stores text while C prepares a response for the browser.
The program keeps appliances in the appliances array and bill records in the bills array. appliance_count and bill_count track how many entries are currently in each array.

These arrays are in memory while the server runs. To keep the information after the server stops, the program saves it to CSV files in the data folder.
2. How C reads form inputs
When you submit a website form, the browser sends its values as encoded text, for example:
name=Fan&room=Bedroom&watts=75&hours=10&quantity=2
These helper functions turn that text into values the program can use:
- url_decode turns encoded text back into readable text.
- param searches for a named value, such as watts.
- pdouble converts a value to a decimal number.
- pint converts a value to a whole number.
The number functions check that the input can be converted. If a value is missing or invalid, they return a fallback value.
3. Energy and bill calculations
Monthly energy estimate
appliance_kwh calculates an appliance’s estimated monthly consumption:
watts × hours per day × quantity × 30 ÷ 1,000
For example, two 75-watt fans used for 10 hours per day:
75 × 10 × 2 × 30 ÷ 1,000 = 45 kWh per month
The program assumes a 30-day month. total_kwh loops through the appliance list, calculates each appliance’s use, and adds it all together.
Electricity bill estimate
bill_cost applies the example tariff rates to different ranges of electricity use, then adds the fixed charge.

With example limits of 100 and 200 units, a total of 180 units is calculated as:
First 100 units × first rate
+ next 80 units × second rate
+ fixed charge
The rates are editable examples for the project. The calculation does not retrieve current BESCOM rates or produce an official bill.
Savings simulation
api_simulate compares the home’s current monthly energy use with a proposed change to one appliance. It removes that appliance’s current estimated use from the total, adds its proposed use, and compares the bill estimates before and after.
Peak-load estimate
api_peak adds the rated wattage of selected appliances that might run at the same time, including their quantity. It divides the total watts by 1,000 to convert to kilowatts.

This estimates connected load from appliance ratings. It does not measure the home’s actual power use.
4. Saving and loading records
save_data writes the appliance and bill arrays into CSV files:
data/appliances.csv
data/monthly_history.csv
load_data reads those files when the C server starts. If there are no appliances in the file, it loads sample appliances to show on the website.

When you enter a past bill, api_history saves it by month and year. If you enter the same month and year again, it updates that record instead of adding a duplicate. Bill details are entered manually; the program does not connect to BESCOM.
5. How the website uses C functions
Each api_... function handles a feature request from the browser:
C function	What it does
api_state	Sends the appliance list and bill history to the browser.
api_appliance	Adds, edits, or deletes an appliance.
api_history	Adds, updates, or deletes a manual bill record.
api_calculate	Calculates estimated energy and bill cost, checks the budget, and compares saved history.
api_simulate	Compares current use with a proposed appliance change.
api_peak	Estimates connected peak load for selected appliances.
api_report	Creates and saves the monthly text report.


json_string, addf, and json_response help C prepare and send JSON. JSON is a text format that JavaScript can read, such as:
{"units": 180, "amount": 1110}
The browser’s JavaScript reads that response and displays the result on the page.
6. How requests reach those functions
handle receives a browser request and checks its path. For example, a request for /api/appliance goes to api_appliance. A request for / returns the main HTML page, while requests for /style.css and /app.js return the other website files.

response sends the HTTP status, headers, and body back to the browser. HTTP is the request-and-response method browsers and web servers use to communicate.
7. How the program starts
main is the starting point of the C program. It:
1. Loads saved appliance and bill records.
2. Creates a network socket for browser communication.
3. Attaches it to 127.0.0.1 on port 8080.
4. Waits for browser connections.
5. Passes each connection to handle.
127.0.0.1 means the same computer. This is why the current website is local: the computer running server.c is also the computer opening the website in the browser.
The key idea
The C code is divided into helpers and feature functions. Helpers read inputs, calculate values, save files, and prepare responses. The api_... functions use those helpers to complete website actions. handle routes browser requests to the correct feature, and main keeps the server running.

      
