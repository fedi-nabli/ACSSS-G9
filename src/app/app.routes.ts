import { Routes } from '@angular/router';
import { HomePage } from './home.page';
import { FormPage } from './form.page';

export const routes: Routes = [
  { path: '', pathMatch: 'full', component: HomePage },
  { path: 'form', component: FormPage },
  { path: '**', redirectTo: '' }
];
