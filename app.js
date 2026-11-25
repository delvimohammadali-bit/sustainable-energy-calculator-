const $ = (s, root = document) => root.querySelector(s);
const $$ = (s, root = document) => [...root.querySelectorAll(s)];
const months = ['January','February','March','April','May','June','July','August','September','October','November','December'];
let state = { appliances: [], bills: [] };
let latestReport = '';

function esc(value) { return String(value ?? '').replace(/[&<>"']/g, c => ({'&':'&amp;','<':'&lt;','>':'&gt;','"':'&quot;',"'":'&#39;'}[c])); }
function money(n) { return Number(n || 0).toLocaleString('en-IN', {minimumFractionDigits:2, maximumFractionDigits:2}); }
function notice(text, error = false) { const el=$('#notice'); el.textContent=text; el.className=`notice show ${error?'error':'success'}`; clearTimeout(notice.timer); notice.timer=setTimeout(()=>el.className='notice',4500); }
function formParams(form) { return new URLSearchParams(new FormData(form)); }
async function api(path, data) {
  const options = data ? {method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body:data instanceof URLSearchParams?data.toString():new URLSearchParams(data).toString()} : {};
  const response = await fetch(path, options);
  if (!response.ok) throw new Error(`Server returned ${response.status}`);
  return response.json();
}
function initDates() {
  const now=new Date(), month=now.getMonth()+1, year=now.getFullYear();
  for (const id of ['calc-month','history-month','report-month']) {
    const el=$(`#${id}`); el.innerHTML=months.map((m,i)=>`<option value="${i+1}" ${i+1===month?'selected':''}>${m}</option>`).join('');
  }
  $('#calc-year').value=$('#history-year').value=$('#report-year').value=year;
}
function showPage(page) {
  $$('.page').forEach(p=>p.classList.toggle('active',p.id===`page-${page}`));
  $$('.nav-item').forEach(b=>b.classList.toggle('active',b.dataset.page===page));
  const current=$(`.nav-item[data-page="${page}"]`); $('#page-title').textContent=current?current.textContent.trim():'Overview';
  if(page==='calculator') calculate();
  window.scrollTo({top:0,behavior:'smooth'});
}
async function refresh() {
  try { state=await api('/api/state'); render(); }
  catch(e) { notice('Cannot reach the C website server. Start it with ./run.sh from the website folder.',true); }
}
function render() {
  renderDashboard(); renderAppliances(); renderHistory(); renderSimSelect();
}
function renderDashboard() {
  const list=state.appliances||[], bills=state.bills||[], total=list.reduce((s,a)=>s+Number(a.kwh),0), average=bills.length?bills.reduce((s,b)=>s+Number(b.amount),0)/bills.length:0;
  $('#dash-kwh').textContent=total.toFixed(2); $('#dash-appliances').textContent=list.length; $('#dash-average').textContent=money(average); $('#dash-bills').textContent=bills.length?bills.length:'No';
  const top=[...list].sort((a,b)=>b.kwh-a.kwh)[0];
  $('#top-insight').innerHTML=top?`<strong>${esc(top.name)}</strong><span>Largest estimated user at ${Number(top.kwh).toFixed(2)} kWh/month. Review its daily hours or consider an efficient model.</span>`:'<strong>No appliances yet</strong><span>Add your household appliances to get a consumption overview.</span>';
  const max=Math.max(1,...list.map(a=>Number(a.kwh)));
  $('#usage-chart').innerHTML=list.length?list.slice(0,10).map(a=>`<div class="chart-col" title="${esc(a.name)}: ${Number(a.kwh).toFixed(2)} kWh"><div class="chart-bar" style="height:${Math.max(5,Number(a.kwh)/max*75)}%"><span class="chart-value">${Number(a.kwh).toFixed(1)}</span></div><span class="chart-label">${esc(a.name)}</span></div>`).join(''):'<div class="empty-row" style="width:100%;align-self:center">Add appliances to see the monthly energy chart.</div>';
  $('#dash-appliance-table').innerHTML=list.length?list.slice(0,6).map(a=>`<tr><td>${esc(a.name)}</td><td>${esc(a.room)}</td><td>${Number(a.watts).toFixed(0)} W</td><td>${Number(a.hours).toFixed(1)} h</td><td>${a.quantity}</td><td><span class="kwh-pill">${Number(a.kwh).toFixed(2)} kWh</span></td><td><button class="small-action" data-edit="${a.id}">Edit</button></td></tr>`).join(''):'<tr><td colspan="7" class="empty-row">Your appliance list is empty.</td></tr>';
}
function applianceRows() {
  const list=state.appliances||[];
  $('#appliance-table').innerHTML=list.length?list.map(a=>`<tr><td>${a.id}</td><td>${esc(a.name)}</td><td>${esc(a.room)}</td><td>${Number(a.watts).toFixed(1)} W</td><td>${Number(a.hours).toFixed(1)} h</td><td>${a.quantity}</td><td><span class="kwh-pill">${Number(a.kwh).toFixed(2)}</span></td><td><div class="row-actions"><button class="small-action" data-edit="${a.id}">Edit</button><button class="small-action delete" data-delete-appliance="${a.id}">Delete</button></div></td></tr>`).join(''):'<tr><td colspan="8" class="empty-row">No appliances saved yet.</td></tr>';
  $('#appliance-count').textContent=list.length;
  $('#peak-help').textContent=list.length?`Available IDs: ${list.map(a=>a.id).join(', ')}.`:'Add appliances first; there are no available IDs.';
}
function renderAppliances(){ applianceRows(); }
function renderHistory() {
  const list=[...(state.bills||[])].sort((a,b)=>b.year-a.year||b.month-a.month);
  $('#history-table').innerHTML=list.length?list.map(b=>`<tr><td>${months[b.month-1]}</td><td>${b.year}</td><td>${Number(b.units).toFixed(2)}</td><td>₹ ${money(b.amount)}</td><td><button class="small-action delete" data-delete-bill="${b.id}">Delete</button></td></tr>`).join(''):'<tr><td colspan="5" class="empty-row">No past bills entered yet.</td></tr>';
  $('#history-count').textContent=list.length;
  const summary=list.length?`Average use: ${(list.reduce((s,b)=>s+Number(b.units),0)/list.length).toFixed(2)} kWh/month · average bill: ₹${money(list.reduce((s,b)=>s+Number(b.amount),0)/list.length)}.`:'Enter a few months to see how your household use changes over time.';
  $('#history-summary-text').textContent=summary;
}
function renderSimSelect() {
  const select=$('#sim-id'), previous=select.value;
  select.innerHTML=(state.appliances||[]).map(a=>`<option value="${a.id}">${a.id}. ${esc(a.name)} — ${esc(a.room)}</option>`).join('');
  if(previous) select.value=previous;
  fillScenario();
}
function fillScenario() { const a=(state.appliances||[]).find(x=>String(x.id)===$('#sim-id').value); if(a){$('#sim-watts').value=a.watts;$('#sim-hours').value=a.hours;} }
function tariffParams() {
  const p=new URLSearchParams();
  for(const id of ['limit1','limit2','rate1','rate2','rate3','fixed','budget'])p.set(id,$(`#${id}`).value);
  return p;
}
function addTariff(p) { const t=tariffParams(); for(const [k,v] of t)p.set(k,v); return p; }
async function calculate() {
  const p=addTariff(new URLSearchParams({month:$('#calc-month').value,year:$('#calc-year').value}));
  try { const r=await api('/api/calculate',p); $('#estimate-amount').textContent=money(r.amount); $('#estimate-units').textContent=Number(r.units).toFixed(2); const status=$('#budget-status');status.className=`budget-status ${r.withinBudget?'':'over'}`;status.textContent=r.withinBudget?`Within budget · ₹${money(r.budget-r.amount)} remaining`:`Over budget by ₹${money(r.amount-r.budget)}`;
    $('#history-comparison').textContent=r.historyFound?`Saved bill: ${Number(r.historyUnits).toFixed(2)} kWh · ₹${money(r.historyAmount)}. Estimate difference: ${r.difference>=0?'+':''}₹${money(r.difference)}.`:'No manual bill is saved for this month. Add it in Monthly history to compare.';
  } catch(e) { notice('Could not calculate. Check that the C server is running.',true); }
}
function resetApplianceForm(){ $('#appliance-form').reset();$('#appliance-id').value='';$('#appliance-quantity').value='1';$('#appliance-form-title').textContent='Add an appliance';$('#appliance-submit').innerHTML='Add appliance <span>→</span>';$('#cancel-edit').classList.add('hidden'); }
function editAppliance(id) { const a=(state.appliances||[]).find(x=>x.id===Number(id));if(!a)return;$('#appliance-id').value=a.id;$('#appliance-name').value=a.name;$('#appliance-room').value=a.room;$('#appliance-watts').value=a.watts;$('#appliance-hours').value=a.hours;$('#appliance-quantity').value=a.quantity;$('#appliance-form-title').textContent=`Edit appliance #${a.id}`;$('#appliance-submit').innerHTML='Save changes <span>→</span>';$('#cancel-edit').classList.remove('hidden');showPage('appliances');$('#appliance-name').focus(); }

$('#main-nav').addEventListener('click',e=>{const btn=e.target.closest('[data-page]');if(btn)showPage(btn.dataset.page);});
document.addEventListener('click',async e=>{
  const goto=e.target.closest('[data-goto]');if(goto)showPage(goto.dataset.goto);
  const edit=e.target.closest('[data-edit]');if(edit)editAppliance(edit.dataset.edit);
  const delA=e.target.closest('[data-delete-appliance]');if(delA&&confirm('Delete this appliance from your inventory?')){const p=new URLSearchParams({action:'delete',id:delA.dataset.deleteAppliance});try{const r=await api('/api/appliance',p);notice(r.message,!r.ok);await refresh();}catch{notice('Could not delete appliance.',true);}}
  const delB=e.target.closest('[data-delete-bill]');if(delB&&confirm('Delete this monthly bill record?')){const p=new URLSearchParams({action:'delete',id:delB.dataset.deleteBill});try{const r=await api('/api/history',p);notice(r.message,!r.ok);await refresh();}catch{notice('Could not delete bill record.',true);}}
});
$('#refresh-button').addEventListener('click',refresh);
$('#appliance-form').addEventListener('submit',async e=>{e.preventDefault();const p=formParams(e.currentTarget);p.set('action',$('#appliance-id').value?'edit':'add');try{const r=await api('/api/appliance',p);notice(r.message,!r.ok);if(r.ok){resetApplianceForm();await refresh();}}catch{notice('Could not save appliance. Check the server connection.',true);}});
$('#cancel-edit').addEventListener('click',resetApplianceForm);
$('#calculator-form').addEventListener('submit',e=>{e.preventDefault();calculate();});
$('#sim-id').addEventListener('change',fillScenario);
$('#sim-form').addEventListener('submit',async e=>{e.preventDefault();const p=addTariff(formParams(e.currentTarget));try{const r=await api('/api/simulate',p);if(!r.ok){notice(r.message,true);return;}$('#sim-placeholder').classList.add('hidden');$('#sim-result').classList.remove('hidden');$('#sim-kwh-change').textContent=`${r.savedKwh>=0?'−':'+'}${Math.abs(r.savedKwh).toFixed(2)} kWh / month`;$('#sim-direction').textContent=r.savedKwh>=0?'↓':'↑';$('#sim-direction').classList.toggle('up',r.savedKwh<0);$('#sim-before-cost').textContent=`₹${money(r.beforeCost)}`;$('#sim-after-cost').textContent=`₹${money(r.afterCost)}`;$('#sim-before-kwh').textContent=`${Number(r.beforeKwh).toFixed(2)} kWh`;$('#sim-after-kwh').textContent=`${Number(r.afterKwh).toFixed(2)} kWh`;$('#sim-saving').textContent=r.savedCost>=0?`Estimated bill reduction: ₹${money(r.savedCost)} per month.`:`Estimated bill increase: ₹${money(Math.abs(r.savedCost))} per month.`;}catch{notice('Could not run the simulation.',true);}});
$('#history-form').addEventListener('submit',async e=>{e.preventDefault();try{const r=await api('/api/history',formParams(e.currentTarget));notice(r.message,!r.ok);if(r.ok){e.currentTarget.reset();$('#history-month').value=new Date().getMonth()+1;$('#history-year').value=new Date().getFullYear();await refresh();}}catch{notice('Could not save bill history.',true);}});
$('#peak-form').addEventListener('submit',async e=>{e.preventDefault();const p=formParams(e.currentTarget);try{const r=await api('/api/peak',p);if(!r.ok){notice(r.message,true);return;}$('#peak-value').textContent=Number(r.peakKw).toFixed(3);const status=$('#peak-status');status.classList.toggle('alert',r.overLimit);status.textContent=r.limitKw>0?(r.overLimit?'Estimated load exceeds the limit.':'Estimated load is within the limit.'):'Add a limit to compare this load.';$('#peak-detail').textContent=`Based on ${r.count} selected appliance(s), including their quantities. Nameplate ratings are used as a conservative connected-load estimate.`;const scale=r.limitKw||3;$('#peak-meter-fill').style.width=`${Math.min(100,Number(r.peakKw)/scale*100)}%`;}catch{notice('Could not estimate the peak load.',true);}});
$('#report-form').addEventListener('submit',async e=>{e.preventDefault();const p=addTariff(formParams(e.currentTarget));try{const r=await api('/api/report',p);if(!r.ok){notice(r.message,true);return;}latestReport=r.content;$('#report-preview').textContent=r.content;$('#report-filename').textContent=`Saved as ${r.filename}`;$('#report-download').classList.remove('hidden');notice('Monthly report generated and saved on this computer.');}catch{notice('Could not generate the report.',true);}});
$('#download-report').addEventListener('click',()=>{const blob=new Blob([latestReport],{type:'text/plain;charset=utf-8'});const url=URL.createObjectURL(blob);const a=document.createElement('a');a.href=url;a.download=`smart_energy_monthly_report_${$('#report-year').value}_${String($('#report-month').value).padStart(2,'0')}.txt`;a.click();URL.revokeObjectURL(url);});
$('#export-history').addEventListener('click',()=>{const rows=[['Month','Year','Units (kWh)','Bill amount (INR)'],...(state.bills||[]).map(b=>[months[b.month-1],b.year,b.units,b.amount])];const csv=rows.map(row=>row.map(v=>`"${String(v).replaceAll('"','""')}"`).join(',')).join('\n');const url=URL.createObjectURL(new Blob([csv],{type:'text/csv'}));const a=document.createElement('a');a.href=url;a.download='monthly_bill_history.csv';a.click();URL.revokeObjectURL(url);});

initDates();refresh();
