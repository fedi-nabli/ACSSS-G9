import { Component } from '@angular/core';
import { RouterLink } from '@angular/router';

@Component({
  standalone: true,
  imports: [RouterLink],
  template: `
    <section class="page-card">
      <h2>Welcome</h2>
      <p>Routing is now enabled between two pages. Use the navigation above to switch views.</p>
      <a class="button" routerLink="form">Open password form</a>
    </section>
  `
})
export class HomePage {}
